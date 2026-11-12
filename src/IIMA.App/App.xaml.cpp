// IINA PlayerCore.active/lastActive/createPlayerCore and AppDelegate.newWindow.
// Copyright (c) 2016-2025 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "App.g.cpp"
namespace winrt::IIMA::implementation
{
App::App(std::shared_ptr<iima::InstanceRouter> router, std::vector<std::wstring> paths)
    : router_(std::move(router)), initialPaths_(std::move(paths)) { InitializeComponent(); }

App::~App() = default;

winrt::com_ptr<MainWindow> App::CreatePlayerWindow()
{
    auto id = windowRegistry_.Register();
    if (!id) throw winrt::hresult_error(E_OUTOFMEMORY, L"最多同时打开 16 个播放器窗口；正在关闭的窗口仍占用名额。");
    winrt::com_ptr<MainWindow> window;
    try {
        window = winrt::make_self<MainWindow>();
        windows_.emplace(*id, window);
        auto weak = get_weak();
        window->Closed([weak, id = *id](auto const&, auto const&) {
            if (auto app = weak.get()) app->WindowClosed(id);
        });
        window->ConfigureWindowActions([weak] {
            if (auto app = weak.get()) app->CreatePlayerWindow();
        }, [weak, id = *id] {
            if (auto app = weak.get()) app->ActivateNext(id);
        });
        window->SetActivationClosing([weak, id = *id] {
            if (auto app = weak.get()) {
                app->windowRegistry_.MarkClosing(id);
                app->UpdateWindowCounts();
            }
        });
        window->Activated([weak, id = *id](auto const&, Microsoft::UI::Xaml::WindowActivatedEventArgs const& args) {
            if (args.WindowActivationState() != Microsoft::UI::Xaml::WindowActivationState::Deactivated)
                if (auto app = weak.get()) app->windowRegistry_.Activate(id);
        });
        window->Activate();
        windowRegistry_.Activate(*id);
        UpdateWindowCounts();
        return window;
    } catch (...) {
        // MainWindow may already own a live mpv worker. Close normally; never
        // erase the final owner while its asynchronous release is in flight.
        if (window && windows_.contains(*id)) {
            windowRegistry_.MarkClosing(*id);
            try { window->Close(); } catch (...) {}
        } else {
            windows_.erase(*id);
            windowRegistry_.Remove(*id);
        }
        throw;
    }
}
void App::RouteActivation(std::vector<std::wstring> paths)
{
    winrt::com_ptr<MainWindow> target;
    if (auto id = windowRegistry_.ActiveTarget()) {
        auto found = windows_.find(*id);
        if (found != windows_.end() && !found->second->IsClosing()) target = found->second;
    }
    if (!target) target = CreatePlayerWindow();
    // Each batch stays in one player's queue. No per-file window fanout yet.
    target->ReceiveActivation(std::move(paths));
}
void App::ActivateNext(WindowId current)
{
    if (windows_.empty()) return;
    auto it = windows_.upper_bound(current);
    for (std::size_t n = 0; n < windows_.size(); ++n) {
        if (it == windows_.end()) it = windows_.begin();
        if (!it->second->IsClosing()) {
            it->second->ReceiveActivation({});
            windowRegistry_.Activate(it->first);
            return;
        }
        ++it;
    }
}
void App::UpdateWindowCounts()
{
    for (auto const& [id, window] : windows_) window->UpdateWindowCount(windows_.size());
}
void App::WindowClosed(WindowId id)
{
    // Hold the implementation until the Closed event handler has returned.
    auto found = windows_.find(id);
    if (found == windows_.end()) return;
    auto keepAlive = found->second;
    windows_.erase(found);
    windowRegistry_.Remove(id);
    UpdateWindowCounts();
    if (windows_.empty()) {
        windowRegistry_.BeginShutdown();
        if (router_) router_->StopAccepting();
    }
}
void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
{
    auto window = CreatePlayerWindow();
    auto dispatcher = window->DispatcherQueue();
    auto weak = get_weak();
    RouteActivation(std::move(initialPaths_));
    router_->SetReceiver([weak, dispatcher](std::vector<std::wstring> paths) {
        return dispatcher.TryEnqueue([weak, paths = std::move(paths)]() mutable {
            if (auto app = weak.get()) {
                try { app->RouteActivation(std::move(paths)); }
                catch (winrt::hresult_error const& error) {
                    MessageBoxW(nullptr, error.message().c_str(), L"IIMA 打开请求失败", MB_OK | MB_ICONWARNING);
                } catch (...) {
                    MessageBoxW(nullptr, L"无法创建或激活播放器窗口。", L"IIMA", MB_OK | MB_ICONWARNING);
                }
            }
        });
    });
}
}
