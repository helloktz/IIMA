#include "pch.h"
#include "VideoPresenter.h"
#include <microsoft.ui.xaml.media.dxinterop.h>
#include <algorithm>
#include <cmath>

namespace iima {
VideoPresenter::VideoPresenter(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel panel,
                               Resize resize, Error error)
    : panel_(std::move(panel)), resize_(std::move(resize)), error_(std::move(error))
{
    sizeToken_ = panel_.SizeChanged([this](auto const&, auto const&) { OnSizeChanged(); });
    scaleToken_ = panel_.CompositionScaleChanged([this](auto const&, auto const&) { OnSizeChanged(); });
}
VideoPresenter::~VideoPresenter()
{
    panel_.SizeChanged(sizeToken_);
    panel_.CompositionScaleChanged(scaleToken_);
    Detach();
}
void VideoPresenter::Bind(winrt::com_ptr<IDXGISwapChain> ownedSwapChain)
{
    if (!ownedSwapChain) { Detach(); return; }
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
    } catch (...) { /* Panel keeps its own COM reference if unbind fails. */ }
}
void VideoPresenter::UpdateTransform()
{
    if (!swapChain_) return;
    auto sx = panel_.CompositionScaleX();
    auto sy = panel_.CompositionScaleY();
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0 || sy <= 0)
        throw winrt::hresult_invalid_argument(L"视频面板的 DPI 缩放比例无效。");
    // mpv receives physical-pixel dimensions. Cancel XAML's composition scaling
    // on the composition swapchain so those pixels are not scaled a second time.
    DXGI_MATRIX_3X2_F inverse{1.0f / sx, 0.0f, 0.0f, 1.0f / sy, 0.0f, 0.0f};
    winrt::check_hresult(swapChain_.as<IDXGISwapChain2>()->SetMatrixTransform(&inverse));
}
void VideoPresenter::OnSizeChanged() noexcept
{
    try { UpdateSize(); }
    catch (winrt::hresult_error const& error) {
        // Never let a graphics failure escape a XAML event handler.
        try { if (error_) error_(error.message()); } catch (...) {}
    }
    catch (...) {
        try { if (error_) error_(L"视频输出尺寸更新失败。"); } catch (...) {}
    }
}
void VideoPresenter::UpdateSize()
{
    UpdateTransform();
    auto pixels = [](double logical, float scale) -> unsigned {
        if (!std::isfinite(logical) || !std::isfinite(scale) || logical <= 0 || scale <= 0) return 1;
        return static_cast<unsigned>(std::clamp(std::ceil(logical * scale), 1.0, 16384.0));
    };
    auto width = pixels(panel_.ActualWidth(), panel_.CompositionScaleX());
    auto height = pixels(panel_.ActualHeight(), panel_.CompositionScaleY());
    if (width != width_ || height != height_) {
        if (resize_) resize_(width, height);
        width_ = width; height_ = height;
    }
}
}
