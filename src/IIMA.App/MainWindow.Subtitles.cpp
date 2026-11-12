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
            self->session_->ApplySubtitleStyle(style);
        }
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
