#include "pch.h"
#include "App.xaml.h"
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto const&)
    {
        winrt::make<winrt::IIMA::implementation::App>();
    });
    return 0;
}
