#pragma once
#include "App.g.h"
#include "Platform/InstanceRouter.h"
namespace winrt::IIMA::implementation
{
    struct App : AppT<App>
    {
        App(std::shared_ptr<iima::InstanceRouter> router, std::vector<std::wstring> initialPaths);
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    private:
        Microsoft::UI::Xaml::Window window_{nullptr};
        std::shared_ptr<iima::InstanceRouter> router_;
        std::vector<std::wstring> initialPaths_;
    };
}
