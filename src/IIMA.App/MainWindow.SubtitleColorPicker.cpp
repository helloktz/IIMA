// IINA SidebarSubtitlesPane.SubStyleView.colorAction/updateTextStyle and
// Extensions.NSColor.mpvColorString. Copyright lhc and contributors. GPL-3.0.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.UI.h>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
fire_and_forget MainWindow::ChooseSubtitleColor(unsigned kind) {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || kind > 2 || !ApplySubtitleColorButton().IsEnabled() ||
        session_->Snapshot().media != iima::MediaState::Loaded) co_return;
    pickerOpen_ = true;
    const auto epoch = navigationEpoch_;
    const auto entry = session_->Snapshot().entryId;
    const auto path = session_->Snapshot().path;
    const auto primary = session_->Snapshot().subtitleId;
    const auto secondary = session_->Snapshot().secondarySubtitleId;
    try {
        const char* property = kind == 0 ? "sub-color" : kind == 1 ? "sub-outline-color" : "sub-back-color";
        auto const& values = session_->Snapshot().subtitleStyleValues;
        auto found = values.find(property);
        auto initial = found == values.end() ? std::optional<iima::SubtitleColor>{} : iima::SubtitleColor::ParseMpv(found->second);
        bool observed = initial.has_value();
        auto draft = kind == 0 ? SubtitleColorText() : kind == 1 ? SubtitleOutlineColorText() : SubtitleBackgroundText();
        if (!initial) initial = iima::SubtitleColor::ParseHex(to_string(draft.Text()));
        bool fromDraft = !observed && initial.has_value();
        if (!initial) initial = kind == 0 ? iima::SubtitleColor{1, 1, 1, 1} :
            kind == 1 ? iima::SubtitleColor{0, 0, 0, 1} : iima::SubtitleColor{0, 0, 0, 0};
        Controls::ColorPicker color;
        color.IsAlphaEnabled(true);
        color.IsMoreButtonVisible(false);
        color.IsColorChannelTextInputVisible(true);
        color.IsHexInputVisible(true);
        auto byte = [](double channel) { return static_cast<std::uint8_t>(std::lround(channel * 255)); };
        color.Color(Windows::UI::Color{byte(initial->alpha), byte(initial->red), byte(initial->green), byte(initial->blue)});
        Controls::TextBlock info;
        info.Text((observed ? hstring(L"初始颜色来自当前内核观察值。") : fromDraft ?
            hstring(L"当前内核颜色不可解析，使用编辑草稿作为初值。") : hstring(L"当前颜色未知，使用默认草稿作为初值。")) +
            L" 主副字幕共享样式，ASS内置排版可能覆盖选择。仅确认后提交此颜色，不改变描边宽度。8位sRGB，透明度0完全透明。");
        info.TextWrapping(TextWrapping::Wrap);
        Controls::StackPanel content;
        content.Spacing(8); content.Children().Append(info); content.Children().Append(color);
        Controls::ScrollViewer scroll;
        scroll.MaxHeight(520); scroll.Content(content);
        Controls::ContentDialog dialog;
        subtitleColorDialog_ = dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(kind == 0 ? L"字幕文字颜色" : kind == 1 ? L"字幕描边颜色" : L"字幕背景颜色"));
        dialog.Content(scroll);
        dialog.PrimaryButtonText(L"应用颜色"); dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        auto weak = get_weak();
        dialog.PrimaryButtonClick([weak, epoch, entry, path, primary, secondary, info](auto const&, Controls::ContentDialogButtonClickEventArgs const& args) {
            auto self = weak.get();
            if (!self || self->closing_) { args.Cancel(true); return; }
            auto const& state = self->session_->Snapshot();
            if (state.media != iima::MediaState::Loaded || self->navigationEpoch_ != epoch ||
                state.entryId != entry || state.path != path || state.subtitleId != primary ||
                state.secondarySubtitleId != secondary || !self->ApplySubtitleColorButton().IsEnabled()) {
                args.Cancel(true); info.Text(L"媒体或字幕选择已变化，请取消后重新选择颜色。");
            }
        });
        auto result = co_await dialog.ShowAsync();
        auto const& state = session_->Snapshot();
        if (!closing_ && result == Controls::ContentDialogResult::Primary && state.media == iima::MediaState::Loaded &&
            navigationEpoch_ == epoch && state.entryId == entry && state.path == path &&
            state.subtitleId == primary && state.secondarySubtitleId == secondary && ApplySubtitleColorButton().IsEnabled()) {
            auto chosen = color.Color();
            iima::SubtitleColor value{chosen.R / 255.0, chosen.G / 255.0, chosen.B / 255.0, chosen.A / 255.0};
            iima::SubtitleStyle style;
            if (kind == 0) style.color = value;
            else if (kind == 1) style.outlineColor = value;
            else style.backgroundColor = value;
            // Deliberately do not call ApplySubtitleColorDraft: its outline
            // action also applies width, which this picker does not edit.
            session_->ApplySubtitleStyle(style, {std::string("options/") + property});
            draft.Text(to_hstring(value.HexString())); // draft, never observed success
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    catch (...) { if (!closing_) ShowError(L"无法选择字幕颜色。"); }
    subtitleColorDialog_ = nullptr;
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}
