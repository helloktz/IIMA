#pragma once
#include "MainWindow.g.h"
#include "../IIMA.Core/Playback/PlayerSession.h"
#include "Presentation/VideoPresenter.h"
#include <winrt/Microsoft.UI.Windowing.h>

namespace winrt::IIMA::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
private:
    winrt::fire_and_forget OpenFile();
    void Apply(iima::PlaybackSnapshot const& state);
    void ToggleFullscreen();
    void BeginClose();
    void ShowError(winrt::hstring const& message);
    std::shared_ptr<iima::PlayerSession> session_;
    std::unique_ptr<iima::VideoPresenter> presenter_;
    bool updating_{}, closing_{}, closeComplete_{}, pickerOpen_{}, fullscreen_{};
};
}
namespace winrt::IIMA::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
