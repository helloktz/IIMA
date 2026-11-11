#pragma once
#include <dxgi1_3.h>
#include <functional>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace iima {
// Accepts an owned COM reference from the synchronized mpv acquisition bridge.
// The borrowed integer display-swapchain property is never dereferenced.
class VideoPresenter final {
public:
    using Resize = std::function<void(unsigned, unsigned)>;
    using Error = std::function<void(winrt::hstring const&)>;
    VideoPresenter(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel panel,
                   Resize resize, Error error = {});
    ~VideoPresenter();
    VideoPresenter(VideoPresenter const&) = delete;
    VideoPresenter& operator=(VideoPresenter const&) = delete;
    void Bind(winrt::com_ptr<IDXGISwapChain> ownedSwapChain);
    void Detach() noexcept;
    void UpdateSize();
    bool IsBound() const noexcept { return swapChain_ != nullptr; }
private:
    void OnSizeChanged() noexcept;
    void UpdateTransform();
    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel panel_{nullptr};
    winrt::com_ptr<IDXGISwapChain> swapChain_;
    Resize resize_;
    Error error_;
    winrt::event_token sizeToken_{}, scaleToken_{};
    unsigned width_{}, height_{};
};
}
