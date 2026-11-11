#include "pch.h"
#include "../IIMA.Core/BuildInfo.h"

namespace
{
    struct App : winrt::Microsoft::UI::Xaml::ApplicationT<App>
    {
        void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
        {
            window_ = winrt::Microsoft::UI::Xaml::Window{};
            window_.Title(L"IIMA " + winrt::to_hstring(iima::version()));
            window_.Content(winrt::Microsoft::UI::Xaml::Controls::Grid{});
            window_.Activate();
        }

    private:
        winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto const&)
    {
        winrt::make<App>();
    });
    return 0;
}
