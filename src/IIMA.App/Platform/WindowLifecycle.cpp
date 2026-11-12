#include "pch.h"
#include "WindowLifecycle.h"
#include <commctrl.h>
#include <utility>
#include <cmath>
#include <limits>
#pragma comment(lib,"comctl32.lib")

namespace iima {
struct WindowLifecycle::State : std::enable_shared_from_this<State> {
    HWND window{};
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
    std::function<void(WindowLifecycleState)> changed;
    std::function<void()> settingsChanged, placementInteraction;
    bool settingsPending{};
    WindowLifecycleState value;
    bool closed{};
    std::optional<WindowAspectConstraint> aspectConstraint, dragAspectConstraint;
    static bool IsOwnedDialog(HWND window,HWND candidate) noexcept {
        if(!candidate) return false;
        DWORD process{};
        GetWindowThreadProcessId(candidate,&process);
        return process==GetCurrentProcessId() && GetAncestor(candidate,GA_ROOTOWNER)==window;
    }
    void Publish(WindowLifecycleState next) {
        if(closed || (next.inactive==value.inactive && next.minimized==value.minimized)) return;
        value=next;
        const auto weak=weak_from_this();
        dispatcher.TryEnqueue([weak,next] {
            if(auto self=weak.lock(); self && !self->closed && self->changed)
                self->changed(next);
        });
    }
    void SettingsChanged() {
        if (closed || settingsPending || !settingsChanged) return;
        settingsPending=true;
        const auto weak=weak_from_this();
        try {
            if (!dispatcher.TryEnqueue([weak] {
                if (auto self=weak.lock(); self && !self->closed) {
                    self->settingsPending=false;
                    if (self->settingsChanged) self->settingsChanged();
                }
            })) settingsPending=false;
        } catch (...) { settingsPending=false; }
    }
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,
                                      UINT_PTR subclass,DWORD_PTR reference) noexcept {
        // Hold across callbacks/DefSubclassProc: a close message may release the
        // owning MainWindow adapter while this native frame is still unwinding.
        auto self=reinterpret_cast<State*>(reference)->weak_from_this().lock();
        if(!self) return DefSubclassProc(hwnd,message,wParam,lParam);
        try {
            if (message == WM_ENTERSIZEMOVE) {
                self->dragAspectConstraint.reset();
                RECT current{};
                if (!self->closed && self->aspectConstraint && !IsZoomed(hwnd) && !IsIconic(hwnd) &&
                    GetWindowRect(hwnd,&current) && GetDpiForWindow(hwnd)==self->aspectConstraint->dpi &&
                    std::abs((double(current.right)-current.left)-self->aspectConstraint->projectedOuter.width)<=1 &&
                    std::abs((double(current.bottom)-current.top)-self->aspectConstraint->projectedOuter.height)<=1)
                    self->dragAspectConstraint=self->aspectConstraint;
                self->value.sizing = true;
            } else if (message == WM_EXITSIZEMOVE) {
                self->value.sizing = false;
                self->aspectConstraint.reset(); self->dragAspectConstraint.reset();
                self->SettingsChanged(); // Reproject after the final layout, not during drag.
            } else if (message == WM_DPICHANGED || message == WM_DISPLAYCHANGE) {
                self->aspectConstraint.reset(); self->dragAspectConstraint.reset();
                self->SettingsChanged();
            } else if (message == WM_SIZING && !self->closed && self->value.sizing &&
                       self->dragAspectConstraint && lParam && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
                if (GetDpiForWindow(hwnd)!=self->dragAspectConstraint->dpi) {
                    self->aspectConstraint.reset(); self->dragAspectConstraint.reset();
                } else {
                    auto rect=reinterpret_cast<RECT*>(lParam);
                    MONITORINFO monitor{sizeof(MONITORINFO)};
                    if (GetMonitorInfoW(MonitorFromRect(rect,MONITOR_DEFAULTTONEAREST),&monitor)) {
                        const auto work=monitor.rcWork;
                        auto constrained=ConstrainVideoResize(
                            {double(rect->left),double(rect->top),double(rect->right)-rect->left,double(rect->bottom)-rect->top},
                            {double(work.left),double(work.top),double(work.right)-work.left,double(work.bottom)-work.top},
                            static_cast<SizingEdge>(wParam),*self->dragAspectConstraint);
                        if (constrained) {
                            // Inward rounding keeps the result inside the work
                            // area. Subpixel aspect error is <= two outer pixels.
                            const auto left=std::ceil(constrained->x), top=std::ceil(constrained->y);
                            const auto right=std::floor(constrained->x+constrained->width);
                            const auto bottom=std::floor(constrained->y+constrained->height);
                            constexpr auto low=double(std::numeric_limits<LONG>::lowest());
                            constexpr auto high=double(std::numeric_limits<LONG>::max());
                            if (left>=low && top>=low && right<=high && bottom<=high && right>left && bottom>top) {
                                *rect={static_cast<LONG>(left),static_cast<LONG>(top),static_cast<LONG>(right),static_cast<LONG>(bottom)};
                                return TRUE; // Native sizing owns application, no MoveAndResize loop.
                            }
                        }
                    }
                }
            }
            if (message == WM_ENTERSIZEMOVE || (message == WM_SYSCOMMAND &&
                ((wParam & 0xfff0) == SC_MOVE || (wParam & 0xfff0) == SC_SIZE ||
                 (wParam & 0xfff0) == SC_MAXIMIZE || (wParam & 0xfff0) == SC_MINIMIZE ||
                 (wParam & 0xfff0) == SC_RESTORE))) {
                // Synchronous UI-thread cancellation precedes XAML layout.
                if (!self->closed && self->placementInteraction) self->placementInteraction();
            }
            auto next=self->value;
            if (message==WM_SETTINGCHANGE || message==WM_THEMECHANGED || message==WM_SYSCOLORCHANGE) {
                self->SettingsChanged();
            } else if(message==WM_ACTIVATE) {
                next.inactive=LOWORD(wParam)==WA_INACTIVE &&
                    !IsOwnedDialog(hwnd,reinterpret_cast<HWND>(lParam));
                self->Publish(next);
            } else if(message==WM_ACTIVATEAPP) {
                // Application deactivation covers shell cases with no next HWND.
                if(!wParam) { next.inactive=true; self->Publish(next); }
                else {
                    // Reactivating an owned dialog need not send WM_ACTIVATE to
                    // its disabled owner. Use this thread's active root rather
                    // than leaving the owner permanently marked inactive.
                    const auto active=GetActiveWindow();
                    if(active==hwnd || IsOwnedDialog(hwnd,active)) {
                        next.inactive=false; self->Publish(next);
                    }
                }
            } else if(message==WM_SIZE) {
                next.minimized=wParam==SIZE_MINIMIZED;
                self->Publish(next);
            } else if(message==WM_NCDESTROY) {
                self->closed=true;
                self->aspectConstraint.reset(); self->dragAspectConstraint.reset();
                self->changed={}; self->settingsChanged={}; self->placementInteraction={};
                self->window=nullptr;
                RemoveWindowSubclass(hwnd,Procedure,subclass);
            }
        } catch(...) {
            // Never unwind through the native window procedure. The next
            // lifecycle event can resynchronize state; no playback is guessed.
        }
        return DefSubclassProc(hwnd,message,wParam,lParam);
    }
};
WindowLifecycle::WindowLifecycle(HWND window,
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
    std::function<void(WindowLifecycleState)> changed,std::function<void()> settingsChanged,
    std::function<void()> placementInteraction) {
    if(!window || !dispatcher || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId())
        throw winrt::hresult_invalid_argument(L"Window lifecycle requires the owning UI thread");
    auto state=std::make_shared<State>();
    state->window=window; state->dispatcher=dispatcher; state->changed=std::move(changed);
    state->settingsChanged=std::move(settingsChanged);
    state->placementInteraction=std::move(placementInteraction);
    auto foreground=GetForegroundWindow();
    state->value.inactive=foreground!=window && !State::IsOwnedDialog(window,foreground);
    state->value.minimized=IsIconic(window)!=FALSE;
    if(!SetWindowSubclass(window,State::Procedure,reinterpret_cast<UINT_PTR>(state.get()),
                          reinterpret_cast<DWORD_PTR>(state.get())))
        throw winrt::hresult_error(E_FAIL,L"Cannot observe window lifecycle");
    state_=std::move(state);
}
WindowLifecycle::~WindowLifecycle() { Close(); }
WindowLifecycleState WindowLifecycle::Current() const noexcept { return state_?state_->value:WindowLifecycleState{}; }
void WindowLifecycle::SetAspectConstraint(std::optional<WindowAspectConstraint> constraint) noexcept {
    if (!state_ || state_->closed) return;
    if (!constraint) {
        state_->aspectConstraint.reset(); state_->dragAspectConstraint.reset();
    } else if (!state_->value.sizing) state_->aspectConstraint=std::move(constraint);
}
void WindowLifecycle::Close() noexcept {
    if(!state_) return;
    state_->closed=true;
    state_->aspectConstraint.reset(); state_->dragAspectConstraint.reset();
    state_->changed={}; state_->settingsChanged={}; state_->placementInteraction={};
    if(state_->window) RemoveWindowSubclass(state_->window,State::Procedure,reinterpret_cast<UINT_PTR>(state_.get()));
    state_->window=nullptr;
    state_.reset();
}
}
