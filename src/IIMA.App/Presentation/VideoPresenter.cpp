#include "pch.h"
#include "VideoPresenter.h"
#include <microsoft.ui.xaml.media.dxinterop.h>
#include <algorithm>
#include <cmath>

namespace iima {
VideoPresenter::VideoPresenter(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel panel, Resize resize)
    : panel_(std::move(panel)), resize_(std::move(resize))
{
    sizeToken_ = panel_.SizeChanged([this](auto const&, auto const&) { UpdateSize(); });
    scaleToken_ = panel_.CompositionScaleChanged([this](auto const&, auto const&) { UpdateSize(); });
}
VideoPresenter::~VideoPresenter()
{
    panel_.SizeChanged(sizeToken_);
    panel_.CompositionScaleChanged(scaleToken_);
    Detach();
}
void VideoPresenter::Bind(winrt::com_ptr<IDXGISwapChain> ownedSwapChain)
{
    // SetSwapChain retains its own reference; retain ours until after unbind.
    auto native = panel_.as<ABI::Microsoft::UI::Xaml::Controls::ISwapChainPanelNative>();
    winrt::check_hresult(native->SetSwapChain(ownedSwapChain.get()));
    swapChain_ = std::move(ownedSwapChain);
    width_ = height_ = 0;
    UpdateSize();
}
void VideoPresenter::Detach() noexcept
{
    try {
        auto native = panel_.as<ABI::Microsoft::UI::Xaml::Controls::ISwapChainPanelNative>();
        if (SUCCEEDED(native->SetSwapChain(nullptr))) swapChain_ = nullptr;
    } catch (...) { /* Destruction must not throw. Panel retains its COM reference. */ }
}
void VideoPresenter::UpdateSize()
{
    auto pixels = [](double logical, float scale) -> unsigned {
        if (!std::isfinite(logical) || !std::isfinite(scale) || logical <= 0 || scale <= 0) return 1;
        return static_cast<unsigned>(std::clamp(std::ceil(logical * scale), 1.0, 16384.0));
    };
    auto width = pixels(panel_.ActualWidth(), panel_.CompositionScaleX());
    auto height = pixels(panel_.ActualHeight(), panel_.CompositionScaleY());
    if (width != width_ || height != height_) {
        width_ = width; height_ = height;
        if (resize_) resize_(width, height);
    }
}
}
