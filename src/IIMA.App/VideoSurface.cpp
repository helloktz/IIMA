#include "pch.h"
#include "VideoSurface.h"
#include "VideoSurface.g.cpp"
namespace winrt::IIMA::implementation {
VideoSurface::VideoSurface() {
    arrow_ = Microsoft::UI::Input::InputSystemCursor::Create(Microsoft::UI::Input::InputSystemCursorShape::Arrow);
    ProtectedCursor(arrow_);
    // No global cursor counter or process-wide native cursor hook. The property
    // belongs to the video hit-test surface, not its siblings or other windows.
    auto weak = get_weak();
    Unloaded([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SetCursorHidden(false);
    });
}
void VideoSurface::SetCursorHidden(bool hidden) {
    if (hidden == hidden_) return;
    if (hidden) ProtectedCursor(nullptr);
    else ProtectedCursor(arrow_);
    hidden_ = hidden;
}
}
