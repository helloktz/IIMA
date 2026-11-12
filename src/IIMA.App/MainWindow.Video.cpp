// Video adjustment actions derived from IINA SidebarVideoPane / PlayerCore.
// Copyright © 2016 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireVideoControls() {
    WireVideoSize();
    auto weak = get_weak();
    FlipVideoButton().Click([weak](auto const&, auto const&) {
        if (auto self=weak.get(); self && !self->closing_) self->session_->ToggleVideoOrientation(false);
    });
    MirrorVideoButton().Click([weak](auto const&, auto const&) {
        if (auto self=weak.get(); self && !self->closing_) self->session_->ToggleVideoOrientation(true);
    });
    ApplyAspectButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_)
            self->session_->SetVideoAspect(to_string(self->AspectText().Text()));
    });
    ResetAspectButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            self->AspectText().Text(L"");
            self->session_->SetVideoAspect("");
        }
    });
    ApplyRotationButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto index = self->RotationBox().SelectedIndex();
            if (index >= 0 && index <= 3) self->session_->SetVideoRotation(index * 90);
        }
    });
    ApplyFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_)
            self->session_->AddVideoFilter(to_string(self->VideoFilterText().Text()));
    });
    RemoveFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_)
            self->session_->RemoveVideoFilter(to_string(self->VideoFilterLabelText().Text()));
    });
}
}
