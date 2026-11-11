#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "App.g.cpp"
#include <shellapi.h>
#include <filesystem>
#pragma comment(lib, "shell32.lib")
namespace winrt::IIMA::implementation
{
    App::App() { InitializeComponent(); }
    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        auto window = winrt::make_self<MainWindow>();
        window_ = window.as<Microsoft::UI::Xaml::Window>();
        window_.Activate();
        int count{};
        if (auto arguments = CommandLineToArgvW(GetCommandLineW(), &count)) {
            std::vector<std::string> paths;
            try {
                for (int i = 1; i < count; ++i) {
                    auto path = std::filesystem::absolute(arguments[i]);
                    if (std::filesystem::is_regular_file(path)) paths.push_back(winrt::to_string(path.wstring()));
                }
            } catch (std::filesystem::filesystem_error const&) { /* Invalid startup paths do not prevent launch. */ }
            LocalFree(arguments);
            window->QueueFiles(std::move(paths));
        }
    }
}
