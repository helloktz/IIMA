// IINA PlayerCore.abLoopA/abLoopB and PlaySliderLoopKnob editing adaptation.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Playback/PlaybackActions.h"
#include <cmath>
#include <limits>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool CanEditLoop(iima::PlaybackSnapshot const& state) {
    return state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded &&
        state.seekable && state.duration && std::isfinite(*state.duration) &&
        *state.duration >= iima::ABLoopState::MinimumEditedPoint;
}
hstring PointText(std::optional<double> point) {
    return point ? to_hstring(*point) : hstring(L"未设置或不可读取");
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireABLoopEditor() {
    WireABLoopMarkers();
    auto weak = get_weak();
    LoadABDraftButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->LoadABLoopDraft();
    });
    ApplyABPointAButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplyABLoopDraft(false);
    });
    ApplyABPointBButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplyABLoopDraft(true);
    });
}
void MainWindow::LoadABLoopDraft() {
    auto const& state = session_->Snapshot();
    abDraftEpoch_.reset();
    if (!closing_ && CanEditLoop(state)) {
        abDraftEpoch_ = navigationEpoch_;
        abDraftIntent_ = session_->ABLoopEditIntentRevision();
        abDraftA_ = state.abLoopA;
        abDraftB_ = state.abLoopB;
        ABPointABox().Value(abDraftA_.value_or(std::numeric_limits<double>::quiet_NaN()));
        ABPointBBox().Value(abDraftB_.value_or(std::numeric_limits<double>::quiet_NaN()));
    }
    UpdateABLoopEditor(state);
}
void MainWindow::UpdateABLoopEditor(iima::PlaybackSnapshot const& state) {
    const bool ready = !closing_ && CanEditLoop(state);
    if (!ready || (abDraftEpoch_ && (*abDraftEpoch_ != navigationEpoch_ || abDraftIntent_ != session_->ABLoopEditIntentRevision() ||
        abDraftA_ != state.abLoopA || abDraftB_ != state.abLoopB))) abDraftEpoch_.reset();
    const bool draft = ready && abDraftEpoch_.has_value();
    LoadABDraftButton().IsEnabled(ready && (state.abLoopA || state.abLoopB));
    ABPointABox().IsEnabled(draft && abDraftA_.has_value());
    ABPointBBox().IsEnabled(draft && abDraftA_.has_value() && abDraftB_.has_value());
    ApplyABPointAButton().IsEnabled(ABPointABox().IsEnabled());
    ApplyABPointBButton().IsEnabled(ABPointBBox().IsEnabled());
    // Do not rewrite the editors on time-pos notifications. Feedback is separate
    // and comes only from mpv observations, never from a submitted command.
    auto text = L"当前 A=" + PointText(state.abLoopA) + L"；B=" + PointText(state.abLoopB);
    if (!draft) text = text + L"。请建立端点并重新读取后编辑。";
    else text = text + L"。每次应用后重新读取；0秒按 IINA 调整为0.000001秒。";
    if (state.abLoopCount == "0") text = text + L" 循环次数为0，当前不会重复。";
    if (state.abLoopA && state.abLoopB && *state.abLoopA == *state.abLoopB)
        text = text + L" 两个端点相同，当前不会形成有效区间。";
    ABEditStatus().Text(text);
    UpdateABLoopMarkers();
}
void MainWindow::ApplyABLoopDraft(bool pointB) {
    auto const& state = session_->Snapshot();
    if (closing_ || !abDraftEpoch_ || *abDraftEpoch_ != navigationEpoch_ || abDraftIntent_ != session_->ABLoopEditIntentRevision() ||
        !CanEditLoop(state) || abDraftA_ != state.abLoopA || abDraftB_ != state.abLoopB) {
        abDraftEpoch_.reset();
        UpdateABLoopEditor(state);
        return;
    }
    const double seconds = pointB ? ABPointBBox().Value() : ABPointABox().Value();
    if (!std::isfinite(seconds) || seconds < 0 || seconds > *state.duration) {
        ShowError(L"端点必须是从0到当前时长之间的有限秒数。");
        return;
    }
    const bool accepted = session_->EditABLoopPoint(pointB, seconds, abDraftA_, abDraftB_);
    // One independently applied endpoint per read. A failed command still uses
    // observed values on the next read; no pretend rollback or atomic pair.
    abDraftEpoch_.reset();
    UpdateABLoopEditor(session_->Snapshot());
    if (!accepted) ShowError(L"端点已改变、尚未建立或命令未被接受，请重新读取。");
}
}
