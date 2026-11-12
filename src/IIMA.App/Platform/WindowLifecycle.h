#pragma once
// Windows adaptation of IINA MainWindowController window activation and
// miniaturization delegates. GPL-3.0; references/iina/LICENSE.
#include <windows.h>
#include "../../IIMA.Core/Geometry/AspectResize.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <functional>
#include <memory>

namespace iima {
struct WindowLifecycleState { bool inactive{}; bool minimized{}; bool sizing{}; };
class WindowLifecycle final {
public:
    // Construct and destroy on the window's UI thread. Queued callbacks cannot
    // outlive Close. Owned dialogs do not count as switching to another player.
    WindowLifecycle(HWND window,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
        std::function<void(WindowLifecycleState)> changed,
        std::function<void()> settingsChanged = {},
        std::function<void()> placementInteraction = {});
    ~WindowLifecycle();
    WindowLifecycle(WindowLifecycle const&)=delete;
    WindowLifecycle& operator=(WindowLifecycle const&)=delete;
    WindowLifecycleState Current() const noexcept;
    // UI thread only. A null projection cancels a live drag immediately. New
    // non-null projections during a drag are ignored, never mixed with layout
    // callbacks whose ActualWidth/Height may belong to the previous frame.
    void SetAspectConstraint(std::optional<WindowAspectConstraint> constraint) noexcept;
    void Close() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
