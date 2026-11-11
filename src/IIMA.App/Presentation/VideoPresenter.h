#pragma once
#include <dxgi.h>
#include <functional>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace iima {
// Owns only a caller-provided COM reference. Never casts display-swapchain's
// borrowed integer address: the current mpv property has no lifetime lease.
class VideoPresenter final {
public:
    using Resize = std::function<void(unsigned, unsigned)>;
    VideoPresenter(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel panel, Resize resize);
    ~VideoPresenter();
    VideoPresenter(VideoPresenter const&) = delete;
    VideoPresenter& operator=(VideoPresenter const&) = delete;
    void Bind(winrt::com_ptr<IDXGISwapChain> ownedSwapChain);
    void Detach() noexcept;
    void UpdateSize();
    bool IsBound() const noexcept { return swapChain_ != nullptr; }
private:
    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel panel_{nullptr};
    winrt::com_ptr<IDXGISwapChain> swapChain_;
    Resize resize_;
    winrt::event_token sizeToken_{}, scaleToken_{};
    unsigned width_{}, height_{};
};
}
