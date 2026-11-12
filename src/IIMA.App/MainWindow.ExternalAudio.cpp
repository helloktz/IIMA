// IINA SidebarAudioPane.loadExternalAudioAction / PlayerCore.loadExternalAudioFile.
// Copyright © 2016 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::LoadExternalAudio() { LoadExternalAV(false); co_return; }
}
