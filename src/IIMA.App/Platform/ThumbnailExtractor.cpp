// Windows adaptation of IINA PlayerCore.generateThumbnails / FFmpegController.
// IINA business rules copyright (c) 2017 lhc and contributors, GPL-3.0.
#include "pch.h"
#include "ThumbnailExtractor.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <objidl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {
constexpr std::uint64_t MaximumPixelsBytes = 128ull * 1024 * 1024;
constexpr std::uint64_t MaximumJpegBytes = 8ull * 1024 * 1024;
struct Cancelled {};
void CheckCancelled(std::stop_token stop) { if (stop.stop_requested()) throw Cancelled{}; }
struct Apartment {
    bool initialized{};
    Apartment() {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        winrt::check_hresult(hr); // worker must not be an STA/UI thread
        initialized = true;
    }
    ~Apartment() { if (initialized) CoUninitialize(); }
};
struct MediaFoundation {
    MediaFoundation() { winrt::check_hresult(MFStartup(MF_VERSION, MFSTARTUP_FULL)); }
    ~MediaFoundation() { MFShutdown(); }
};
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
// A read-only IStream over an already verified file HANDLE. Passing this to MF
// avoids reopening a path through a URL resolver after verification. Clones
// have independent cursors over one handle, with all physical seeks serialized.
struct FileState {
    Handle handle;
    std::mutex mutex;
    std::uint64_t size{};
};
class FileStream final : public IStream {
    std::atomic<ULONG> references_{1};
    std::shared_ptr<FileState> file_;
    std::uint64_t position_{};
public:
    explicit FileStream(std::shared_ptr<FileState> file, std::uint64_t position = 0)
        : file_(std::move(file)), position_(position) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_ISequentialStream || iid == IID_IStream) {
            *result = static_cast<IStream*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Read(void* bytes, ULONG count, ULONG* read) override {
        if (read) *read = 0;
        if (!bytes && count) return STG_E_INVALIDPOINTER;
        std::scoped_lock lock(file_->mutex);
        if (position_ > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) return STG_E_SEEKERROR;
        LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>(position_);
        if (!SetFilePointerEx(file_->handle.value, offset, nullptr, FILE_BEGIN)) return HRESULT_FROM_WIN32(GetLastError());
        DWORD actual{};
        if (!ReadFile(file_->handle.value, bytes, count, &actual, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        position_ += actual;
        if (read) *read = actual;
        return actual == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(void const*, ULONG, ULONG* written) override {
        if (written) *written = 0; return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* result) override {
        std::scoped_lock lock(file_->mutex);
        std::uint64_t base{};
        if (origin == STREAM_SEEK_CUR) base = position_;
        else if (origin == STREAM_SEEK_END) base = file_->size;
        else if (origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;
        if (move.QuadPart < 0) {
            const auto magnitude = static_cast<std::uint64_t>(-(move.QuadPart + 1)) + 1;
            if (magnitude > base) return STG_E_SEEKERROR;
            position_ = base - magnitude;
        } else {
            const auto delta = static_cast<std::uint64_t>(move.QuadPart);
            if (base > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()) - delta) return STG_E_SEEKERROR;
            position_ = base + delta;
        }
        if (result) result->QuadPart = position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_REVERTED; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* result, DWORD) override {
        if (!result) return STG_E_INVALIDPOINTER;
        *result = {}; result->type = STGTY_STREAM;
        result->cbSize.QuadPart = file_->size; result->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        try {
            std::scoped_lock lock(file_->mutex);
            *result = new FileStream(file_, position_); return S_OK;
        } catch (...) { return E_OUTOFMEMORY; }
    }
};
winrt::com_ptr<IStream> OpenLocalFile(std::filesystem::path const& path) {
    const auto text = path.native();
    if (text.size() < 3 || text.find(L'\0') != std::wstring::npos ||
        !((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) ||
        text[1] != L':' || (text[2] != L'\\' && text[2] != L'/') || text.find(L':', 2) != std::wstring::npos)
        throw winrt::hresult_invalid_argument(L"Thumbnail preview requires an absolute local file path");
    wchar_t root[]{text[0], L':', L'\\', L'\0'};
    const auto drive = GetDriveTypeW(root);
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE)
        throw winrt::hresult_error(E_ACCESSDENIED, L"Thumbnail preview does not read network drives");
    // No reparse traversal for background preview. The opened handle is checked
    // again below, so a rename does not swap the bytes MF subsequently reads.
    auto normalized = path.lexically_normal();
    std::filesystem::path prefix = normalized.root_path();
    for (auto const& component : normalized.relative_path()) {
        prefix /= component;
        const auto attributes = GetFileAttributesW(prefix.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) winrt::throw_last_error();
        if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))
            throw winrt::hresult_error(E_ACCESSDENIED, L"Remote, offline and linked files are excluded from background previews");
    }
    auto file = std::make_shared<FileState>();
    file->handle.value = CreateFileW(normalized.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file->handle.value == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    FILE_ATTRIBUTE_TAG_INFO info{};
    winrt::check_bool(GetFileInformationByHandleEx(file->handle.value, FileAttributeTagInfo, &info, sizeof(info)));
    if (info.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))
        throw winrt::hresult_error(E_ACCESSDENIED, L"Not an ordinary local video file");
    std::wstring finalPath(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(file->handle.value, finalPath.data(),
        static_cast<DWORD>(finalPath.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= finalPath.size()) throw winrt::hresult_error(E_ACCESSDENIED, L"Cannot verify thumbnail source location");
    finalPath.resize(length);
    if (finalPath.size() < 7 || finalPath.compare(0, 4, L"\\\\?\\") != 0 || finalPath[5] != L':')
        throw winrt::hresult_error(E_ACCESSDENIED, L"Thumbnail source is not a local drive file");
    root[0] = finalPath[4];
    const auto finalDrive = GetDriveTypeW(root);
    if (finalDrive != DRIVE_FIXED && finalDrive != DRIVE_REMOVABLE)
        throw winrt::hresult_error(E_ACCESSDENIED, L"Thumbnail source resolved to a remote drive");
    LARGE_INTEGER size{};
    winrt::check_bool(GetFileSizeEx(file->handle.value, &size));
    if (size.QuadPart <= 0) throw winrt::hresult_invalid_argument(L"Empty thumbnail source");
    file->size = static_cast<std::uint64_t>(size.QuadPart);
    winrt::com_ptr<IStream> stream; stream.attach(new FileStream(std::move(file)));
    return stream;
}
struct FrameLayout {
    UINT32 width{}, height{};
    LONG stride{};
    UINT32 aspectNumerator{1}, aspectDenominator{1};
    UINT32 rotation{};
};
FrameLayout Layout(IMFMediaType* type) {
    FrameLayout layout;
    winrt::check_hresult(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &layout.width, &layout.height));
    if (!layout.width || !layout.height || layout.width > 8192 || layout.height > 8192 ||
        static_cast<std::uint64_t>(layout.width) * layout.height * 4 > MaximumPixelsBytes)
        throw winrt::hresult_error(E_OUTOFMEMORY, L"Video frame exceeds thumbnail decode limits");
    UINT32 stride{};
    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) layout.stride = static_cast<LONG>(stride);
    else layout.stride = static_cast<LONG>(layout.width * 4);
    UINT32 numerator{}, denominator{};
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, &numerator, &denominator)) && numerator && denominator) {
        layout.aspectNumerator = numerator; layout.aspectDenominator = denominator;
    }
    type->GetUINT32(MF_MT_VIDEO_ROTATION, &layout.rotation);
    return layout;
}
struct LockedBuffer {
    IMFMediaBuffer* buffer{};
    ~LockedBuffer() { if (buffer) buffer->Unlock(); }
};
struct Locked2D {
    IMF2DBuffer* buffer{};
    ~Locked2D() { if (buffer) buffer->Unlock2D(); }
};
std::vector<std::uint8_t> CopyFrame(IMFSample* sample, FrameLayout const& layout) {
    winrt::com_ptr<IMFMediaBuffer> buffer;
    winrt::check_hresult(sample->ConvertToContiguousBuffer(buffer.put()));
    const auto rowBytes = static_cast<std::size_t>(layout.width) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * layout.height);
    auto plane = buffer.try_as<IMF2DBuffer>();
    if (plane) {
        BYTE* scanline{}; LONG pitch{};
        winrt::check_hresult(plane->Lock2D(&scanline, &pitch));
        Locked2D lock{plane.get()};
        if (!scanline || std::abs(static_cast<std::int64_t>(pitch)) < static_cast<std::int64_t>(rowBytes))
            throw winrt::hresult_error(E_INVALIDARG, L"Invalid thumbnail frame pitch");
        for (UINT32 row = 0; row < layout.height; ++row)
            std::memcpy(pixels.data() + rowBytes * row, scanline + static_cast<std::ptrdiff_t>(pitch) * row, rowBytes);
    } else {
        BYTE* data{}; DWORD length{}, maximum{};
        winrt::check_hresult(buffer->Lock(&data, &maximum, &length));
        LockedBuffer lock{buffer.get()};
        const auto pitch = std::abs(static_cast<std::int64_t>(layout.stride));
        const auto needed = pitch * (layout.height - 1) + rowBytes;
        if (!data || pitch < static_cast<std::int64_t>(rowBytes) || needed > length)
            throw winrt::hresult_error(E_INVALIDARG, L"Truncated thumbnail frame");
        for (UINT32 row = 0; row < layout.height; ++row) {
            const auto sourceRow = layout.stride < 0 ? layout.height - row - 1 : row;
            std::memcpy(pixels.data() + rowBytes * row, data + pitch * sourceRow, rowBytes);
        }
    }
    return pixels;
}
std::vector<std::uint8_t> EncodeJpeg(std::vector<std::uint8_t>& pixels,
    FrameLayout const& layout, iima::ThumbnailRequest const& request, UINT32& width, UINT32& height) {
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(factory->CreateBitmapFromMemory(layout.width, layout.height,
        GUID_WICPixelFormat32bppBGR, layout.width * 4, static_cast<UINT>(pixels.size()), pixels.data(), bitmap.put()));
    const bool quarterTurn = layout.rotation == 90 || layout.rotation == 270;
    double displayWidth = static_cast<double>(layout.width) * layout.aspectNumerator / layout.aspectDenominator;
    double displayHeight = layout.height;
    if (quarterTurn) std::swap(displayWidth, displayHeight);
    if (!std::isfinite(displayWidth) || displayWidth <= 0 || displayHeight <= 0)
        throw winrt::hresult_invalid_argument(L"Invalid thumbnail display aspect");
    const auto scale = std::min({1.0, request.maximumWidth / displayWidth, request.maximumHeight / displayHeight});
    width = static_cast<UINT32>(std::clamp(std::llround(displayWidth * scale), 1ll, static_cast<long long>(request.maximumWidth)));
    height = static_cast<UINT32>(std::clamp(std::llround(displayHeight * scale), 1ll, static_cast<long long>(request.maximumHeight)));
    winrt::com_ptr<IWICBitmapSource> source = bitmap.as<IWICBitmapSource>();
    winrt::com_ptr<IWICBitmapFlipRotator> rotation;
    if (layout.rotation == 90 || layout.rotation == 180 || layout.rotation == 270) {
        winrt::check_hresult(factory->CreateBitmapFlipRotator(rotation.put()));
        const auto transform = layout.rotation == 90 ? WICBitmapTransformRotate90 :
            layout.rotation == 180 ? WICBitmapTransformRotate180 : WICBitmapTransformRotate270;
        winrt::check_hresult(rotation->Initialize(source.get(), transform));
        source = rotation.as<IWICBitmapSource>();
    }
    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::check_hresult(factory->CreateBitmapScaler(scaler.put()));
    winrt::check_hresult(scaler->Initialize(source.get(), width, height, WICBitmapInterpolationModeFant));
    winrt::com_ptr<IWICFormatConverter> converter;
    winrt::check_hresult(factory->CreateFormatConverter(converter.put()));
    winrt::check_hresult(converter->Initialize(scaler.get(), GUID_WICPixelFormat24bppBGR,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    winrt::com_ptr<IStream> memory;
    winrt::check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, memory.put()));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(memory.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::com_ptr<IPropertyBag2> options;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), options.put()));
    PROPBAG2 property{}; property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT quality{}; quality.vt = VT_R4; quality.fltVal = 0.75f;
    winrt::check_hresult(options->Write(1, &property, &quality));
    winrt::check_hresult(frame->Initialize(options.get()));
    winrt::check_hresult(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat24bppBGR)
        throw winrt::hresult_error(WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT, L"JPEG encoder rejected BGR format");
    winrt::check_hresult(frame->WriteSource(converter.get(), nullptr));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
    STATSTG stat{};
    winrt::check_hresult(memory->Stat(&stat, STATFLAG_NONAME));
    if (!stat.cbSize.QuadPart || stat.cbSize.QuadPart > MaximumJpegBytes)
        throw winrt::hresult_error(E_OUTOFMEMORY, L"Encoded thumbnail exceeds its budget");
    std::vector<std::uint8_t> jpeg(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    winrt::check_hresult(memory->Seek(zero, STREAM_SEEK_SET, nullptr));
    ULONG read{};
    winrt::check_hresult(memory->Read(jpeg.data(), static_cast<ULONG>(jpeg.size()), &read));
    if (read != jpeg.size()) throw winrt::hresult_error(E_FAIL, L"Truncated encoded thumbnail");
    return jpeg;
}
}
namespace iima {
ThumbnailExtractionResult ExtractThumbnail(ThumbnailRequest const& request, std::stop_token stop) {
    ThumbnailExtractionResult result;
    result.generation = request.generation; result.requestedSeconds = request.seconds;
    try {
        CheckCancelled(stop);
        if (!std::isfinite(request.seconds) || request.seconds < 0 || request.seconds > 9.0e11 ||
            !request.maximumWidth || !request.maximumHeight || request.maximumWidth > 1024 || request.maximumHeight > 1024)
            throw winrt::hresult_invalid_argument(L"Invalid thumbnail time or dimensions");
        Apartment apartment;
        MediaFoundation mediaFoundation;
        auto stream = OpenLocalFile(request.path);
        winrt::com_ptr<IMFByteStream> bytes;
        winrt::check_hresult(MFCreateMFByteStreamOnStream(stream.get(), bytes.put()));
        winrt::com_ptr<IMFAttributes> attributes;
        winrt::check_hresult(MFCreateAttributes(attributes.put(), 3));
        winrt::check_hresult(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
        winrt::check_hresult(attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE));
        winrt::check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE));
        winrt::com_ptr<IMFSourceReader> reader;
        winrt::check_hresult(MFCreateSourceReaderFromByteStream(bytes.get(), attributes.get(), reader.put()));
        CheckCancelled(stop);
        winrt::check_hresult(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE));
        winrt::check_hresult(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));
        winrt::com_ptr<IMFMediaType> nativeType;
        winrt::check_hresult(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nativeType.put()));
        const auto nativeLayout = Layout(nativeType.get()); // reject oversized decode before negotiating RGB
        winrt::com_ptr<IMFMediaType> output;
        winrt::check_hresult(MFCreateMediaType(output.put()));
        winrt::check_hresult(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        winrt::check_hresult(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
        winrt::check_hresult(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.get()));
        PROPVARIANT position{}; position.vt = VT_I8;
        position.hVal.QuadPart = static_cast<LONGLONG>(std::llround(request.seconds * 10000000.0));
        winrt::check_hresult(reader->SetCurrentPosition(GUID_NULL, position));
        std::vector<std::uint8_t> pixels;
        FrameLayout selectedLayout;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        // Bound iterations and cooperative wall-clock work. Native calls can
        // themselves exceed this bound; it is not a codec watchdog guarantee.
        for (unsigned iteration = 0; iteration < 180; ++iteration) {
            CheckCancelled(stop);
            if (std::chrono::steady_clock::now() >= deadline) break;
            DWORD flags{}; LONGLONG timestamp{};
            winrt::com_ptr<IMFSample> sample;
            winrt::check_hresult(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                nullptr, &flags, &timestamp, sample.put()));
            if (flags & MF_SOURCE_READERF_ERROR) throw winrt::hresult_error(E_FAIL, L"Video decoder failed during thumbnail extraction");
            if (sample) {
                winrt::com_ptr<IMFMediaType> current;
                winrt::check_hresult(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, current.put()));
                auto layout = Layout(current.get());
                // Some RGB processors omit source rotation/pixel aspect metadata.
                if (!layout.rotation) layout.rotation = nativeLayout.rotation;
                if (layout.aspectNumerator == 1 && layout.aspectDenominator == 1) {
                    layout.aspectNumerator = nativeLayout.aspectNumerator;
                    layout.aspectDenominator = nativeLayout.aspectDenominator;
                }
                pixels = CopyFrame(sample.get(), layout); selectedLayout = layout;
                result.actualSeconds = std::max(0.0, static_cast<double>(timestamp) / 10000000.0);
                if (timestamp >= position.hVal.QuadPart) break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        CheckCancelled(stop);
        if (pixels.empty()) throw winrt::hresult_error(MF_E_END_OF_STREAM, L"No preview frame is available at this position");
        result.jpeg = EncodeJpeg(pixels, selectedLayout, request, result.width, result.height);
        CheckCancelled(stop);
        result.status = ThumbnailExtractionStatus::Ready;
    } catch (Cancelled const&) {
        result.status = ThumbnailExtractionStatus::Cancelled; result.jpeg.clear();
    } catch (winrt::hresult_error const& error) {
        result.status = error.code() == MF_E_TOPO_CODEC_NOT_FOUND || error.code() == MF_E_UNSUPPORTED_BYTESTREAM_TYPE ||
            error.code() == MF_E_INVALIDMEDIATYPE ? ThumbnailExtractionStatus::Unsupported : ThumbnailExtractionStatus::Failed;
        result.error = error.message().c_str(); result.jpeg.clear();
    } catch (std::bad_alloc const&) {
        result.error = L"Thumbnail preview exhausted its memory budget"; result.jpeg.clear();
    } catch (std::exception const&) {
        result.error = L"Thumbnail preview could not read this file"; result.jpeg.clear();
    }
    return result;
}
}
