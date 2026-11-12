// Windows adaptation of IINA PlayerCore.generateThumbnails / FFmpegController.
// IINA business rules copyright (c) 2017 lhc and contributors, GPL-3.0.
#include "pch.h"
#include "ThumbnailExtractor.h"
#include "PlaylistDurationProbe.h"
#include <iterator>
#include <string_view>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <propsys.h>
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
// Instantiate SDK PROPERTYKEY constants here, rather than relying on implicit
// linker exports. Keep after other interface headers to limit INITGUID scope.
#include <initguid.h>
#include <propkey.h>
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
    // Optional duration-probe budget shared by every IStream clone.
    std::optional<std::uint64_t> remainingReadBytes;
    std::stop_token stop;
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
        if (file_->stop.stop_requested()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (file_->remainingReadBytes && count > *file_->remainingReadBytes) return STG_E_READFAULT;
        if (position_ > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) return STG_E_SEEKERROR;
        LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>(position_);
        if (!SetFilePointerEx(file_->handle.value, offset, nullptr, FILE_BEGIN)) return HRESULT_FROM_WIN32(GetLastError());
        DWORD actual{};
        if (!ReadFile(file_->handle.value, bytes, count, &actual, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        if (file_->remainingReadBytes) *file_->remainingReadBytes -= actual;
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
winrt::com_ptr<IStream> OpenLocalFile(std::filesystem::path const& path,
    std::shared_ptr<FileState>* opened = nullptr) {
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
    if (opened) *opened = file;
    winrt::com_ptr<IStream> stream; stream.attach(new FileStream(std::move(file)));
    return stream;
}
// MF's Windows 7+ metadata service returns IPropertyStore from the existing
// media source. This is not SHGetPropertyStoreFromParsingName (no path reopen).
// Contracts: Microsoft Learn, IMFSourceReader::GetServiceForStream,
// medfound/shell-metadata-providers, properties/props-system-music-artist.
struct OwnedProperty {
    PROPVARIANT value{};
    ~OwnedProperty() { PropVariantClear(&value); }
};
std::optional<std::string> MetadataText(wchar_t const* text) {
    if (!text) return {};
    std::size_t length{};
    while (length <= 4096 && text[length]) ++length;
    if (!length || length > 4096) return {};
    std::wstring clean;
    clean.reserve(std::min<std::size_t>(length, 1024));
    for (std::size_t i = 0; i < length && clean.size() < 1024; ++i) {
        const auto ch = static_cast<unsigned>(text[i]);
        if (ch < 0x20 || (ch >= 0x7f && ch <= 0x9f) || ch == 0x2028 || ch == 0x2029 ||
            ch == 0x061c || ch == 0x200e || ch == 0x200f ||
            (ch >= 0x202a && ch <= 0x202e) || (ch >= 0x2066 && ch <= 0x2069)) {
            clean.push_back(L' ');
        } else if (ch >= 0xd800 && ch <= 0xdbff) {
            if (i + 1 < length && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
                if (clean.size() + 2 > 1024) break;
                clean.push_back(text[i]); clean.push_back(text[++i]);
            } else clean.push_back(L'\ufffd');
        } else if (ch >= 0xdc00 && ch <= 0xdfff) clean.push_back(L'\ufffd');
        else clean.push_back(text[i]);
    }
    while (!clean.empty() && clean.back() == L' ') clean.pop_back();
    const auto first = clean.find_first_not_of(L' ');
    if (first == std::wstring::npos) return {};
    if (first) clean.erase(0, first);
    auto utf8 = winrt::to_string(std::wstring_view(clean));
    if (utf8.size() > 1024) {
        std::size_t end = 1024;
        while (end && (static_cast<unsigned char>(utf8[end]) & 0xc0) == 0x80) --end;
        utf8.resize(end);
    }
    return utf8.empty() ? std::nullopt : std::optional<std::string>{std::move(utf8)};
}
std::optional<std::string> MetadataValue(IPropertyStore* store, REFPROPERTYKEY key, bool multiple) {
    OwnedProperty property;
    if (FAILED(store->GetValue(key, &property.value))) return {};
    if (property.value.vt == VT_LPWSTR) return MetadataText(property.value.pwszVal);
    if (!multiple || property.value.vt != (VT_VECTOR | VT_LPWSTR) ||
        !property.value.calpwstr.pElems || property.value.calpwstr.cElems > 16) return {};
    std::string joined;
    for (ULONG i = 0; i < property.value.calpwstr.cElems; ++i) {
        auto value = MetadataText(property.value.calpwstr.pElems[i]);
        if (!value) continue;
        const auto separator = joined.empty() ? 0u : 2u;
        if (joined.size() + separator + value->size() > 1024) break;
        if (separator) joined += "; ";
        joined += *value;
    }
    return joined.empty() ? std::nullopt : std::optional<std::string>{std::move(joined)};
}
std::pair<std::optional<std::string>, std::optional<std::string>> ReadPlaylistMetadata(
    IMFSourceReader* reader, std::stop_token stop) {
    CheckCancelled(stop);
    winrt::com_ptr<IPropertyStore> store;
    if (FAILED(reader->GetServiceForStream(MF_SOURCE_READER_MEDIASOURCE, MF_PROPERTY_HANDLER_SERVICE,
        __uuidof(IPropertyStore), store.put_void())) || !store) return {};
    CheckCancelled(stop);
    auto title = MetadataValue(store.get(), PKEY_Title, false);
    CheckCancelled(stop);
    auto artist = MetadataValue(store.get(), PKEY_Music_Artist, true);
    CheckCancelled(stop);
    return {std::move(title), std::move(artist)};
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
// Duration-only adaptation of IINA PlayerCore.refreshCachedVideoInfo and
// FFmpegController.probeVideoInfo. Installed MF handlers, not FFmpeg, determine
// coverage. This never selects a decoder output, seeks, or calls ReadSample.
PlaylistDurationProbeResult ProbePlaylistDuration(std::filesystem::path const& path, std::stop_token stop) {
    PlaylistDurationProbeResult result;
    try {
        CheckCancelled(stop);
        const auto& text = path.native();
        if (text.empty() || text.size() > 32767) {
            result.status = PlaylistDurationProbeStatus::Unsupported; return result;
        }
        auto extension = path.extension().native();
        for (auto& ch : extension) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        constexpr std::wstring_view allowed[]{L".mp4", L".m4v", L".mov", L".mkv", L".avi", L".wmv",
            L".mp3", L".m4a", L".aac", L".wav", L".flac", L".ogg", L".opus", L".wma",
            L".webm", L".mpg", L".mpeg", L".aiff", L".aif", L".3gp", L".3g2", L".ts", L".m2ts"};
        if (std::find(std::begin(allowed), std::end(allowed), extension) == std::end(allowed)) {
            result.status = PlaylistDurationProbeStatus::Unsupported; return result;
        }
        Apartment apartment;
        MediaFoundation mediaFoundation;
        std::shared_ptr<FileState> file;
        auto stream = OpenLocalFile(path, &file);
        file->remainingReadBytes = 64ull * 1024 * 1024;
        file->stop = stop;
        auto fingerprint = [&] {
            BY_HANDLE_FILE_INFORMATION info{};
            winrt::check_bool(GetFileInformationByHandle(file->handle.value, &info));
            PlaylistDurationFingerprint value;
            value.volumeSerial = info.dwVolumeSerialNumber;
            value.fileIndex = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
            value.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            value.lastWriteTime = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
                info.ftLastWriteTime.dwLowDateTime;
            return value;
        };
        const auto before = fingerprint();
        if (!before.size || before.size > (1ull << 40) || before.size != file->size) {
            result.status = PlaylistDurationProbeStatus::Unsupported; return result;
        }
        CheckCancelled(stop);
        winrt::com_ptr<IMFByteStream> bytes;
        winrt::check_hresult(MFCreateMFByteStreamOnStream(stream.get(), bytes.put()));
        winrt::com_ptr<IMFAttributes> attributes;
        winrt::check_hresult(MFCreateAttributes(attributes.put(), 2));
        winrt::check_hresult(attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE));
        winrt::check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE));
        winrt::com_ptr<IMFSourceReader> reader;
        winrt::check_hresult(MFCreateSourceReaderFromByteStream(bytes.get(), attributes.get(), reader.put()));
        CheckCancelled(stop);
        OwnedProperty duration;
        const auto hr = reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration.value);
        CheckCancelled(stop);
        // Each capability is optional independently. A missing duration does
        // not hide usable tags; missing metadata does not discard duration.
        std::pair<std::optional<std::string>, std::optional<std::string>> metadata;
        try { metadata = ReadPlaylistMetadata(reader.get(), stop); }
        catch (Cancelled const&) { throw; }
        catch (...) {} // optional native metadata service is best effort
        CheckCancelled(stop);
        // Read both snapshots from exactly the HANDLE supplying the byte stream.
        // This detects ordinary concurrent modification, not an adversarial
        // writer restoring size and timestamps or a later path replacement.
        const auto after = fingerprint();
        if (!(before == after)) { result.status = PlaylistDurationProbeStatus::Changed; return result; }
        std::optional<double> seconds;
        if (SUCCEEDED(hr) && duration.value.vt == VT_UI8 && duration.value.uhVal.QuadPart) {
            const auto value = static_cast<double>(duration.value.uhVal.QuadPart) / 10000000.0;
            if (std::isfinite(value) && value > 0) seconds = value;
        }
        if (!seconds && !metadata.first && !metadata.second) {
            result.status = PlaylistDurationProbeStatus::Unsupported; return result;
        }
        CheckCancelled(stop);
        result.seconds = seconds;
        result.title = std::move(metadata.first);
        result.artist = std::move(metadata.second);
        result.fingerprint = after;
        result.status = PlaylistDurationProbeStatus::Ready;
    } catch (Cancelled const&) {
        result.status = PlaylistDurationProbeStatus::Cancelled;
    } catch (winrt::hresult_error const& error) {
        result.status = stop.stop_requested() ? PlaylistDurationProbeStatus::Cancelled :
            (error.code() == MF_E_TOPO_CODEC_NOT_FOUND || error.code() == MF_E_UNSUPPORTED_BYTESTREAM_TYPE ||
            error.code() == MF_E_INVALIDMEDIATYPE ? PlaylistDurationProbeStatus::Unsupported : PlaylistDurationProbeStatus::Failed);
    } catch (...) {
        result.status = PlaylistDurationProbeStatus::Failed;
    }
    return result;
}
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
