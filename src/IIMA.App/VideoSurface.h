#pragma once
// IINA MainWindowController.hideUIAndCursor, Windows-only cursor presentation.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "VideoSurface.g.h"
#include <winrt/Microsoft.UI.Input.h>
namespace winrt::IIMA::implementation {
struct VideoSurface : VideoSurfaceT<VideoSurface> {
    VideoSurface();
    void SetCursorHidden(bool hidden);
private:
    Microsoft::UI::Input::InputSystemCursor arrow_{nullptr};
    bool hidden_{};
};
}
namespace winrt::IIMA::factory_implementation {
struct VideoSurface : VideoSurfaceT<VideoSurface, implementation::VideoSurface> {};
}
