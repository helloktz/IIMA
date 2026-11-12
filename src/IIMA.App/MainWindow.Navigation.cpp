// IINA MainMenuActions.menuJumpTo and SidebarVideoPane.SpeedView adaptation.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include "../IIMA.Core/Playback/SpeedControl.h"
#include <algorithm>
#include <charconv>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool SeekAvailable(iima::PlaybackSnapshot const& state) {
    return state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded &&
        state.seekable && state.duration && std::isfinite(*state.duration) && *state.duration > 0;
}
std::optional<double> JumpTarget(hstring const& input) {
    auto text = to_string(input);
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    auto last = text.find_last_not_of(" \t\r\n");
    text = text.substr(first, last - first + 1);
    if (text.size() > 64 || std::count(text.begin(), text.end(), ':') > 2) return {};
    // IINA VideoTime tolerates invalid/empty components; the input dialog uses
    // stricter syntax so "typo:30" never unexpectedly jumps to thirty seconds.
    std::size_t start = 0;
    for (;;) {
        auto end = text.find(':', start);
        auto length = end == std::string::npos ? text.size() - start : end - start;
        if (!length) return {};
        auto part = std::string_view(text).substr(start, length);
        bool decimal = false;
        for (char c : part) {
            if (c == '.' && end == std::string::npos && !decimal) { decimal = true; continue; }
            if (c < '0' || c > '9') return {};
        }
        double value{};
        auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || !std::isfinite(value)) return {};
        if (end == std::string::npos) break;
        // VideoTime parses hour/minute components as signed 64-bit integers.
        // Match that bound before using its permissive parser, or an overflowing
        // hour field could otherwise silently become zero.
        std::int64_t integral{};
        auto integer = std::from_chars(part.data(), part.data() + part.size(), integral);
        if (integer.ec != std::errc{} || integer.ptr != part.data() + part.size()) return {};
        start = end + 1;
    }
    auto time = iima::VideoTime::Parse(text);
    return time && std::isfinite(time->Seconds()) && time->Seconds() >= 0 ?
        std::optional<double>(time->Seconds()) : std::nullopt;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireNavigationControls() {
    auto weak = get_weak();
    JumpToTimeButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->JumpToTime();
    });
    JumpToBeginningButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && SeekAvailable(self->session_->Snapshot()))
            self->session_->Seek(0);
    });
    ApplyCustomSpeedButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplyCustomSpeed(self->CustomSpeedBox().Value());
    });
    ResetCustomSpeedButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplyCustomSpeed(1);
    });
    CustomSpeedSlider().ValueChanged([weak](auto const&, auto const& args) {
        if (auto self = weak.get(); self && !self->projectingNavigation_ && !self->closing_) {
            if (auto speed = iima::SpeedFromSlider(args.NewValue())) self->ApplyCustomSpeed(*speed);
        }
    });
}
void MainWindow::ApplyCustomSpeed(double requested) {
    if (closing_ || session_->Snapshot().session != iima::SessionState::Ready) return;
    auto speed = iima::NormalizePlaybackSpeed(requested);
    if (!speed) { ShowError(L"请输入0.01到100之间的有限倍速数值。"); return; }
    session_->SetSpeed(*speed);
    userData_.settings.speed = *speed;
    MarkUserDataDirty();
    // Display only the command value in the editor. The separate current label
    // is always sourced from mpv observations, including after an error.
    CustomSpeedBox().Value(*speed);
}
void MainWindow::UpdateNavigationControls(iima::PlaybackSnapshot const& state) {
    struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(projectingNavigation_);
    if (state.media != iima::MediaState::Loaded || navigationPath_ != state.path || navigationEntry_ != state.entryId) {
        ++navigationEpoch_;
        navigationPath_ = state.path; navigationEntry_ = state.entryId;
    }
    JumpToTimeButton().IsEnabled(!closing_ && SeekAvailable(state));
    JumpToBeginningButton().IsEnabled(!closing_ && SeekAvailable(state));
    const bool ready = !closing_ && state.session == iima::SessionState::Ready;
    CustomSpeedBox().IsEnabled(ready);
    ApplyCustomSpeedButton().IsEnabled(ready);
    ResetCustomSpeedButton().IsEnabled(ready);
    CustomSpeedSlider().IsEnabled(ready);
    auto slider = iima::SpeedToSliderPosition(state.speed);
    if (slider) CustomSpeedSlider().Value(*slider);
    // Do not overwrite a partly edited NumberBox on every time-pos notification.
    // Readback uses its own label; leaving the editor does not submit a change.
    CustomSpeedStatus().Text(L"当前倍速 " + to_hstring(state.speed) +
        L"×；滑杆范围0.25到16，精确输入支持0.01到100。");
}
winrt::fire_and_forget MainWindow::JumpToTime() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || !SeekAvailable(session_->Snapshot())) co_return;
    pickerOpen_ = true;
    auto epoch = navigationEpoch_;
    auto entry = session_->Snapshot().entryId;
    auto path = session_->Snapshot().path;
    try {
        Controls::ContentDialog dialog;
        jumpDialog_ = dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(L"跳转到时间"));
        dialog.PrimaryButtonText(L"跳转"); dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(Controls::ContentDialogButton::Primary);
        Controls::TextBox input;
        input.MaxLength(64);
        if (auto position = session_->Snapshot().position)
            input.Text(to_hstring(iima::VideoTime(*position).StringRepresentation(3)));
        input.PlaceholderText(L"时:分:秒.毫秒，或秒数");
        Controls::TextBlock message;
        message.Text(L"输入秒、分:秒或时:分:秒，支持小数秒。播放可以继续，取消不跳转。");
        message.TextWrapping(TextWrapping::Wrap);
        Controls::StackPanel content;
        content.Spacing(8); content.Children().Append(input); content.Children().Append(message);
        dialog.Content(content);
        auto weak = get_weak();
        dialog.PrimaryButtonClick([weak, input, message, epoch, entry, path](auto const&, Controls::ContentDialogButtonClickEventArgs const& args) {
            auto self = weak.get();
            if (!self || self->closing_) { args.Cancel(true); return; }
            auto const& state = self->session_->Snapshot();
            if (self->navigationEpoch_ != epoch || state.entryId != entry || state.path != path || !SeekAvailable(state)) {
                args.Cancel(true); message.Text(L"媒体已变化或不能跳转，请取消后重新打开。"); return;
            }
            auto target = JumpTarget(input.Text());
            if (!target || *target > *state.duration) {
                args.Cancel(true); message.Text(L"请输入从0到当前时长之间的有效时间。");
            }
        });
        auto result = co_await dialog.ShowAsync();
        auto const& state = session_->Snapshot();
        if (!closing_ && result == Controls::ContentDialogResult::Primary && navigationEpoch_ == epoch &&
            state.entryId == entry && state.path == path && SeekAvailable(state)) {
            if (auto target = JumpTarget(input.Text()); target && *target <= *state.duration)
                session_->Seek(*target);
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    catch (...) { if (!closing_) ShowError(L"无法打开时间跳转窗口。"); }
    jumpDialog_ = nullptr;
    pickerOpen_ = false;
}
}
