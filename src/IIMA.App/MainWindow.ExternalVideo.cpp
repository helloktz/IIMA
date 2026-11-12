// IINA JavascriptAPICore.TrackAPI.loadTrack / PlayerCore.loadExternalVideoFile.
// Copyright © 2016, 2018 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::LoadExternalVideo() { LoadExternalAV(true); co_return; }
}
