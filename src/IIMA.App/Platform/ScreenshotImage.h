// Windows adaptation of IINA PlayerCore.createImage / screenshotCallback.
// IINA logic: Copyright (c) 2016 lhc and contributors, GPL-3.0.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace iima {
struct ScreenshotPixels {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    // Top-down straight-alpha BGRA, not premultiplied. No COM lifetime escapes.
    std::vector<std::uint8_t> bgra;
};
// Call on a COM-initialized worker, not the UI thread. Uses installed WIC
// decoders. PNG/JPEG normally work; JXL/AVIF/WebP depend on registered codecs.
// A missing codec is an error, not a reason to remove a successfully saved file.
ScreenshotPixels DecodeScreenshot(const std::filesystem::path& source,
    std::size_t maximumBytes = 128 * 1024 * 1024);
// Worker-side bounded preview, premultiplied BGRA for XAML WriteableBitmap.
// No upscale; maximum 300 x 200 pixels. Does not mutate clipboard pixels.
ScreenshotPixels MakeScreenshotPreview(const ScreenshotPixels& pixels);
// UI-thread only with a live HWND owned by the calling thread. Copies pixels
// into CF_DIBV5; after success Windows owns the HGLOBAL. Does not retain source.
// OpenClipboard failure leaves previous clipboard contents intact. A failure
// after EmptyClipboard cannot restore the previous owner's delayed formats.
void CopyScreenshotToClipboard(HWND owner, const ScreenshotPixels& pixels);
}
