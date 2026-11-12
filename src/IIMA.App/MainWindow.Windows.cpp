// IINA PlayerCore.active/newPlayerCore and AppDelegate.newWindow adaptation.
// Copyright (c) 2016-2025 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::IIMA::implementation {
void MainWindow::ConfigureWindowActions(std::function<void()> create,
                                        std::function<void()> next)
{
    newWindow_ = std::move(create);
    nextWindow_ = std::move(next);
    auto weak = get_weak();
    NewWindowButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && self->newWindow_) {
            try { self->newWindow_(); }
            catch (winrt::hresult_error const& e) { self->ShowError(e.message()); }
            catch (std::exception const& e) { self->ShowError(winrt::to_hstring(e.what())); }
        }
    });
    NextWindowButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && self->nextWindow_) self->nextWindow_();
    });
}
void MainWindow::UpdateWindowCount(std::size_t count)
{
    if (closing_) return;
    WindowCountText().Text(L"播放器窗口：" + winrt::to_hstring(count));
    NewWindowButton().IsEnabled(count < 16);
    NextWindowButton().IsEnabled(count > 1);
    RefreshPlayerMenus();
}
}
