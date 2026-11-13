#pragma once
#include "App.xaml.g.h"
#include "Platform/InstanceRouter.h"
#include "../IIMA.Core/Windows/PlayerWindowRegistry.h"
#include <map>
namespace winrt::IIMA::implementation
{
    struct MainWindow;
    struct App : AppT<App>
    {
        App(std::shared_ptr<iima::InstanceRouter> router, std::vector<std::wstring> initialPaths);
        ~App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    private:
        using WindowId = iima::PlayerWindowRegistry::WindowId;
        winrt::com_ptr<MainWindow> CreatePlayerWindow();
        void RouteActivation(std::vector<std::wstring> paths);
        void RouteResolvedInput(WindowId source, std::vector<std::string> paths,
                                bool newWindow, bool group, bool allowDuplicates, bool singleExplicitMedia);
        void ActivateNext(WindowId current);
        void WindowClosed(WindowId id);
        void UpdateWindowCounts();
        // Strong ownership lasts through asynchronous mpv/user-data shutdown,
        // not just until the user's first close request.
        std::map<WindowId, winrt::com_ptr<MainWindow>> windows_;
        iima::PlayerWindowRegistry windowRegistry_;
        std::shared_ptr<iima::InstanceRouter> router_;
        std::vector<std::wstring> initialPaths_;
    };
}
