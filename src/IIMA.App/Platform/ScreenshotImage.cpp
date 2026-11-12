// Windows adaptation of IINA PlayerCore.createImage / screenshotCallback.
// IINA logic: Copyright (c) 2016 lhc and contributors, GPL-3.0.
#include "pch.h"
#include "ScreenshotImage.h"
#include <wincodec.h>
#include <cstring>
#include <limits>
#pragma comment(lib, "windowscodecs.lib")

namespace {
std::size_t PixelBytes(const iima::ScreenshotPixels& pixels) {
    if (!pixels.width || !pixels.height || pixels.width > 32768 || pixels.height > 32768 ||
        pixels.stride != static_cast<std::uint64_t>(pixels.width) * 4)
        throw winrt::hresult_invalid_argument(L"Invalid screenshot pixel dimensions");
    const auto bytes = static_cast<std::uint64_t>(pixels.stride) * pixels.height;
    if (bytes > std::numeric_limits<DWORD>::max() || bytes != pixels.bgra.size())
        throw winrt::hresult_invalid_argument(L"Invalid screenshot pixel buffer");
    return static_cast<std::size_t>(bytes);
}
struct GlobalBuffer {
    HGLOBAL value{};
    ~GlobalBuffer() { if (value) GlobalFree(value); }
    GlobalBuffer(const GlobalBuffer&) = delete;
    GlobalBuffer& operator=(const GlobalBuffer&) = delete;
    explicit GlobalBuffer(SIZE_T bytes) : value(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes)) {
        if (!value) throw winrt::hresult_error(E_OUTOFMEMORY, L"Cannot allocate clipboard image");
    }
};
struct ClipboardLock {
    explicit ClipboardLock(HWND owner) { winrt::check_bool(OpenClipboard(owner)); }
    ~ClipboardLock() { CloseClipboard(); }
    ClipboardLock(const ClipboardLock&) = delete;
    ClipboardLock& operator=(const ClipboardLock&) = delete;
};
}
namespace iima {
ScreenshotPixels DecodeScreenshot(const std::filesystem::path& source, std::size_t maximumBytes) {
    if (source.empty()) throw winrt::hresult_invalid_argument(L"Empty screenshot path");
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::check_hresult(factory->CreateDecoderFromFilename(source.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, decoder.put()));
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(decoder->GetFrame(0, frame.put()));
    ScreenshotPixels result;
    winrt::check_hresult(frame->GetSize(&result.width, &result.height));
    if (!result.width || !result.height || result.width > 32768 || result.height > 32768)
        throw winrt::hresult_error(E_INVALIDARG, L"Screenshot dimensions exceed limits");
    result.stride = result.width * 4;
    const auto bytes = static_cast<std::uint64_t>(result.stride) * result.height;
    if (bytes > maximumBytes || bytes > std::numeric_limits<UINT>::max())
        throw winrt::hresult_error(E_OUTOFMEMORY, L"Screenshot exceeds the decode memory budget");
    winrt::com_ptr<IWICFormatConverter> converter;
    winrt::check_hresult(factory->CreateFormatConverter(converter.put()));
    winrt::check_hresult(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    result.bgra.resize(static_cast<std::size_t>(bytes));
    winrt::check_hresult(converter->CopyPixels(nullptr, result.stride,
        static_cast<UINT>(bytes), result.bgra.data()));
    return result;
}
void CopyScreenshotToClipboard(HWND owner, const ScreenshotPixels& pixels) {
    if (!owner || !IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId())
        throw winrt::hresult_invalid_argument(L"Clipboard owner must be a live window on this thread");
    const auto bytes = PixelBytes(pixels);
    GlobalBuffer buffer(sizeof(BITMAPV5HEADER) + bytes);
    auto* memory = static_cast<std::uint8_t*>(GlobalLock(buffer.value));
    if (!memory) winrt::throw_last_error();
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(pixels.width);
    header.bV5Height = -static_cast<LONG>(pixels.height); // top-down DIB
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(bytes);
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;
    std::memcpy(memory, &header, sizeof(header));
    std::memcpy(memory + sizeof(header), pixels.bgra.data(), bytes);
    GlobalUnlock(buffer.value);
    ClipboardLock clipboard(owner);
    winrt::check_bool(EmptyClipboard());
    if (!SetClipboardData(CF_DIBV5, buffer.value)) winrt::throw_last_error();
    buffer.value = nullptr;
}
}
