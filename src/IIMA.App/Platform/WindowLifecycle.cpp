#include "pch.h"
#include "WindowLifecycle.h"
#include <commctrl.h>
#include <utility>
#pragma comment(lib,"comctl32.lib")

namespace iima {
struct WindowLifecycle::State : std::enable_shared_from_this<State> {
    HWND window{};
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
    std::function<void(WindowLifecycleState)> changed;
    WindowLifecycleState value;
    bool closed{};
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
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,
                                      UINT_PTR subclass,DWORD_PTR reference) noexcept {
        // Hold across callbacks/DefSubclassProc: a close message may release the
        // owning MainWindow adapter while this native frame is still unwinding.
        auto self=reinterpret_cast<State*>(reference)->weak_from_this().lock();
        if(!self) return DefSubclassProc(hwnd,message,wParam,lParam);
        try {
            auto next=self->value;
            if(message==WM_ACTIVATE) {
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
                self->changed={};
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
    std::function<void(WindowLifecycleState)> changed) {
    if(!window || !dispatcher || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId())
        throw winrt::hresult_invalid_argument(L"Window lifecycle requires the owning UI thread");
    auto state=std::make_shared<State>();
    state->window=window; state->dispatcher=dispatcher; state->changed=std::move(changed);
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
void WindowLifecycle::Close() noexcept {
    if(!state_) return;
    state_->closed=true;
    state_->changed={};
    if(state_->window) RemoveWindowSubclass(state_->window,State::Procedure,reinterpret_cast<UINT_PTR>(state_.get()));
    state_->window=nullptr;
    state_.reset();
}
}
