#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "App.g.cpp"
namespace winrt::IIMA::implementation
{
    App::App(std::shared_ptr<iima::InstanceRouter> router, std::vector<std::wstring> paths)
        : router_(std::move(router)), initialPaths_(std::move(paths)) { InitializeComponent(); }
    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        auto window = winrt::make_self<MainWindow>();
        window_ = window.as<Microsoft::UI::Xaml::Window>();
        window_.Activate();
        auto weak = window->get_weak();
        auto dispatcher = window_.DispatcherQueue();
        std::weak_ptr<iima::InstanceRouter> weakRouter = router_;
        window->SetActivationClosing([weakRouter] { if (auto router = weakRouter.lock()) router->StopAccepting(); });
        window_.Closed([weakRouter](auto const&, auto const&) { if (auto router = weakRouter.lock()) router->StopAccepting(); });
        // Queue initial input first so already-waiting secondary activations keep
        // arrival order. The UI batches rapid external opens like IINA does.
        window->ReceiveActivation(std::move(initialPaths_));
        router_->SetReceiver([weak, dispatcher](std::vector<std::wstring> paths) {
            return dispatcher.TryEnqueue([weak, paths = std::move(paths)]() mutable {
                if (auto current = weak.get()) current->ReceiveActivation(std::move(paths));
            });
        });
    }
}
