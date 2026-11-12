// Subtitle controls derived from IINA SidebarSubtitlesPane / PlayerCore.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireSubtitleControls() {
    auto weak = get_weak();
    MatchSubtitleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->FindLocalSubtitle();
    });
    ApplySubtitleStyleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            iima::SubtitleStyle style;
            style.font = to_string(self->SubtitleFontText().Text());
            style.fontSize = self->SubtitleFontSizeBox().Value();
            style.bold = self->SubtitleBoldCheck().IsChecked().Value();
            style.italic = self->SubtitleItalicCheck().IsChecked().Value();
            if (!style.Valid() || style.font.size() > 1024) { self->ShowError(L"请输入有效字体和字号。"); return; }
            self->session_->ApplySubtitleStyle(style, {"sub-font", "options/sub-font-size", "options/sub-bold", "options/sub-italic"});
        }
    });
    ChooseSubtitleColorButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ChooseSubtitleColor(0);
    });
    ChooseSubtitleOutlineColorButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ChooseSubtitleColor(1);
    });
    ChooseSubtitleBackgroundButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ChooseSubtitleColor(2);
    });
    ApplySubtitleColorButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplySubtitleColorDraft(0);
    });
    ApplySubtitleOutlineButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplySubtitleColorDraft(1);
    });
    ApplySubtitleBackgroundButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplySubtitleColorDraft(2);
    });
    ApplySubtitleLayoutButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto scale = self->SubtitleScaleBox().Value();
            auto position = self->SubtitlePositionBox().Value();
            if (!std::isfinite(scale) || scale < 0.01 || scale > 100 ||
                !std::isfinite(position) || position < 0 || position > 100) {
                self->ShowError(L"请输入有效字幕缩放和位置。");
                return;
            }
            self->session_->SetSubtitleScale(scale);
            self->session_->SetSubtitlePosition(static_cast<int>(position));
        }
    });
    ReloadSubtitlesButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->ReloadSubtitles();
    });
    PreviousSubtitleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->SeekSubtitle(-1);
    });
    NextSubtitleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->SeekSubtitle(1);
    });
}
}
