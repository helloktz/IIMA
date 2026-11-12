// Adapted from IINA ThumbnailCache.swift / CacheManager.swift, (c) 2017 lhc.
// GPL-3.0; references/iina/LICENSE. The cache stores one on-demand MF frame,
// not IINA's complete FFmpeg-generated movie index. No path appears in filenames.
#include "pch.h"
#include "ThumbnailDiskCache.h"
#include "../../IIMA.Core/Thumbnails/ThumbnailCache.h"
#include "../../IIMA.Core/Thumbnails/ThumbnailCacheManager.h"
#include <shlobj.h>
#include <bcrypt.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
std::atomic<std::uint64_t> cacheEpoch{};
constexpr std::uint64_t MaxItemBytes = 8ull * 1024 * 1024 + 64;
constexpr std::size_t MaxDirectoryEntries = 20000;
constexpr std::uint64_t WindowsEpochTicks = 116444736000000000ull;
struct Cancelled {};
void Check(std::stop_token stop) { if (stop.stop_requested()) throw Cancelled{}; }
winrt::handle Open(std::filesystem::path const& path, DWORD access, DWORD share, DWORD flags) {
    auto raw = CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, flags, nullptr);
    if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    return winrt::handle{raw};
}
BY_HANDLE_FILE_INFORMATION Information(HANDLE file, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    winrt::check_bool(GetFileInformationByHandle(file, &info));
    if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) ||
        (!directory && info.nNumberOfLinks != 1))
        throw winrt::hresult_error(E_ACCESSDENIED, L"Cache access requires ordinary local files and directories");
    return info;
}
std::uint64_t Pair(DWORD high, DWORD low) { return (static_cast<std::uint64_t>(high) << 32) | low; }
std::uint64_t Ticks(FILETIME time) { return Pair(time.dwHighDateTime, time.dwLowDateTime); }
struct LockedPath {
    std::filesystem::path path;
    std::vector<winrt::handle> directories;
};
LockedPath LockPath(std::filesystem::path const& original, bool create, bool finalDirectory) {
    auto text = original.native();
    if (text.size() < 3 || text[1] != ':' || (text[2] != '\\' && text[2] != '/') ||
        !((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z')) ||
        text.find(L'\0') != text.npos || text.find(L':', 2) != text.npos)
        throw winrt::hresult_error(E_ACCESSDENIED, L"Cache access requires a local absolute path");
    wchar_t root[]{text[0], L':', L'\\', L'\0'};
    auto drive = GetDriveTypeW(root);
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE)
        throw winrt::hresult_error(E_ACCESSDENIED, L"Network paths are excluded from thumbnail caching");
    auto normalized = original.lexically_normal();
    LockedPath result;
    result.path = normalized;
    auto directory = finalDirectory ? normalized : normalized.parent_path();
    auto prefix = directory.root_path();
    // Holding each ancestor without FILE_SHARE_DELETE closes the replacement
    // race between path inspection and the final CreateFile/MoveFileEx.
    auto hold = [&](std::filesystem::path const& value) {
        if (create && !CreateDirectoryW(value.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            winrt::throw_last_error();
        auto handle = Open(value, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
        Information(handle.get(), true);
        result.directories.push_back(std::move(handle));
    };
    auto rootHandle = Open(prefix, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    Information(rootHandle.get(), true);
    result.directories.push_back(std::move(rootHandle));
    for (auto const& component : directory.relative_path()) {
        auto part = component.native();
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
            throw winrt::hresult_error(E_ACCESSDENIED, L"Ambiguous cache path component");
        prefix /= component;
        hold(prefix);
    }
    return result;
}
std::filesystem::path CacheDirectory() {
    PWSTR local{};
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local));
    struct Release { PWSTR value; ~Release() { CoTaskMemFree(value); } } release{local};
    return std::filesystem::path(local) / L"IIMA" / L"Cache" / L"ThumbnailFrames-v1";
}
struct Source {
    LockedPath location;
    winrt::handle file;
    BY_HANDLE_FILE_INFORMATION info{};
    iima::ThumbnailSourceIdentity legacy;
};
Source OpenSource(std::filesystem::path const& path) {
    Source source;
    source.location = LockPath(path, false, false);
    // A current writer prevents cache use. The handle is kept until extraction
    // ends so the source bytes cannot change or be replaced under our cache key.
    source.file = Open(source.location.path, GENERIC_READ, FILE_SHARE_READ,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS);
    source.info = Information(source.file.get(), false);
    source.legacy.size = Pair(source.info.nFileSizeHigh, source.info.nFileSizeLow);
    if (!source.legacy.size) throw winrt::hresult_invalid_argument(L"Empty thumbnail source");
    auto ticks = Ticks(source.info.ftLastWriteTime);
    // Match Swift truncation toward zero for pre-1970 timestamps as well.
    source.legacy.modifiedUnixSeconds = ticks >= WindowsEpochTicks ?
        static_cast<std::int64_t>((ticks - WindowsEpochTicks) / 10000000) :
        -static_cast<std::int64_t>((WindowsEpochTicks - ticks) / 10000000);
    return source;
}
struct Algorithm { BCRYPT_ALG_HANDLE value{}; ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); } };
struct Hash { BCRYPT_HASH_HANDLE value{}; ~Hash() { if (value) BCryptDestroyHash(value); } };
void Crypto(NTSTATUS status) { if (status < 0) throw winrt::hresult_error(E_FAIL, L"Thumbnail cache hashing failed"); }
std::wstring CacheName(Source const& source, iima::ThumbnailRequest const& request) {
    Algorithm algorithm;
    Crypto(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD length{}, copied{};
    Crypto(BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &copied, 0));
    std::vector<UCHAR> object(length);
    Hash hash;
    Crypto(BCryptCreateHash(algorithm.value, &hash.value, object.data(), length, nullptr, 0, 0));
    auto update = [&](void const* bytes, std::size_t size) {
        if (size > std::numeric_limits<ULONG>::max()) throw winrt::hresult_invalid_argument();
        Crypto(BCryptHashData(hash.value, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(size), 0));
    };
    auto number = [&](std::uint64_t value) {
        std::array<std::uint8_t, 8> bytes{};
        for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
        update(bytes.data(), bytes.size());
    };
    constexpr char revision[] = "IIMA-MF-on-demand-JPEG-v1";
    update(revision, sizeof(revision));
    auto const& path = source.location.path.native();
    number(path.size());
    update(path.data(), path.size() * sizeof(wchar_t));
    number(source.info.dwVolumeSerialNumber);
    number(Pair(source.info.nFileIndexHigh, source.info.nFileIndexLow));
    number(source.legacy.size);
    number(Ticks(source.info.ftLastWriteTime));
    number(Ticks(source.info.ftCreationTime));
    number(std::bit_cast<std::uint64_t>(request.seconds == 0 ? 0.0 : request.seconds));
    number(request.maximumWidth);
    number(request.maximumHeight);
    std::array<UCHAR, 32> digest{};
    Crypto(BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0));
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring result;
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result + L".iimat";
}
bool OwnedName(std::wstring_view name) {
    if (name.size() != 70 || !name.ends_with(L".iimat")) return false;
    return std::all_of(name.begin(), name.begin() + 64, [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
    });
}
struct CacheLock {
    LockedPath directory;
    winrt::handle mutex;
    bool acquired{};
    explicit CacheLock(std::stop_token stop) : directory(LockPath(CacheDirectory(), true, true)) {
        // All IIMA processes for this desktop serialize cache mutations. No
        // native mutex wait happens on the UI thread. Abandoned is recoverable.
        mutex.attach(CreateMutexW(nullptr, FALSE, L"Local\\IIMA.ThumbnailFrames.v1"));
        if (!mutex) winrt::throw_last_error();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        for (;;) {
            Check(stop);
            if (std::chrono::steady_clock::now() >= deadline)
                throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Thumbnail cache is busy");
            auto status = WaitForSingleObject(mutex.get(), 100);
            if (status == WAIT_OBJECT_0 || status == WAIT_ABANDONED) { acquired = true; break; }
            if (status != WAIT_TIMEOUT) winrt::throw_last_error();
        }
    }
    ~CacheLock() { if (acquired) ReleaseMutex(mutex.get()); }
};
struct Apartment {
    HRESULT status{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    Apartment() { winrt::check_hresult(status); }
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
bool ImageSize(std::span<std::uint8_t const> jpeg, std::uint32_t& width, std::uint32_t& height) {
    try {
        Apartment apartment;
        winrt::com_ptr<IWICImagingFactory> factory;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
        winrt::com_ptr<IWICStream> stream;
        winrt::check_hresult(factory->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromMemory(const_cast<BYTE*>(jpeg.data()), static_cast<DWORD>(jpeg.size())));
        winrt::com_ptr<IWICBitmapDecoder> decoder;
        winrt::check_hresult(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.put()));
        GUID format{};
        winrt::check_hresult(decoder->GetContainerFormat(&format));
        if (format != GUID_ContainerFormatJpeg) return false;
        winrt::com_ptr<IWICBitmapFrameDecode> frame;
        winrt::check_hresult(decoder->GetFrame(0, frame.put()));
        winrt::check_hresult(frame->GetSize(&width, &height));
        return width > 0 && height > 0 && width <= 1024 && height <= 1024;
    } catch (...) { return false; }
}
iima::ThumbnailCacheLimits Limits() { return {MaxItemBytes, 8 * 1024 * 1024, 1}; }
std::optional<iima::ThumbnailExtractionResult> Read(CacheLock const& cache, std::wstring const& name,
    Source const& source, iima::ThumbnailRequest const& request, std::stop_token stop) {
    Check(stop);
    auto file = Open(cache.directory.path / name, GENERIC_READ | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN);
    auto info = Information(file.get(), false);
    auto size = Pair(info.nFileSizeHigh, info.nFileSizeLow);
    if (size > MaxItemBytes) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    DWORD read{};
    winrt::check_bool(ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr));
    if (read != bytes.size()) return {};
    auto index = iima::IndexThumbnailCache(bytes, source.legacy, Limits());
    if (!index.index || index.index->entries.size() != 1) return {};
    auto const& record = index.index->entries.front();
    auto jpeg = iima::ThumbnailImageBytes(bytes, record);
    if (!jpeg) return {};
    iima::ThumbnailExtractionResult result;
    if (!ImageSize(*jpeg, result.width, result.height) ||
        result.width > request.maximumWidth || result.height > request.maximumHeight) return {};
    result.status = iima::ThumbnailExtractionStatus::Ready;
    result.generation = request.generation;
    result.requestedSeconds = request.seconds;
    result.actualSeconds = record.seconds;
    result.jpeg.assign(jpeg->begin(), jpeg->end());
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    SetFileTime(file.get(), nullptr, &now, nullptr); // best-effort explicit LRU
    Check(stop);
    return result;
}
struct Entry { std::wstring name; std::uint64_t bytes{}, accessed{}; };
std::vector<Entry> Enumerate(CacheLock const& cache, std::uint64_t& total) {
    std::vector<Entry> entries;
    total = 0;
    WIN32_FIND_DATAW data{};
    auto pattern = cache.directory.path / L"*";
    HANDLE raw = FindFirstFileW(pattern.c_str(), &data);
    if (raw == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return entries;
        winrt::throw_last_error();
    }
    struct Close { HANDLE value; ~Close() { FindClose(value); } } close{raw};
    std::size_t count{};
    do {
        if (++count > MaxDirectoryEntries) throw winrt::hresult_error(E_FAIL, L"Thumbnail cache directory exceeds scan budget");
        if (!OwnedName(data.cFileName) || (data.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))) continue;
        auto bytes = Pair(data.nFileSizeHigh, data.nFileSizeLow);
        if (bytes > std::numeric_limits<std::uint64_t>::max() - total)
            throw winrt::hresult_error(E_FAIL, L"Thumbnail cache size overflow");
        total += bytes;
        entries.push_back({data.cFileName, bytes, Ticks(data.ftLastAccessTime)});
    } while (FindNextFileW(raw, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) winrt::throw_last_error();
    std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) {
        return a.accessed != b.accessed ? a.accessed < b.accessed : a.name < b.name;
    });
    return entries;
}
bool Remove(CacheLock const& cache, Entry const& entry) {
    try {
        auto file = Open(cache.directory.path / entry.name, DELETE | FILE_READ_ATTRIBUTES, 0, FILE_FLAG_OPEN_REPARSE_POINT);
        auto info = Information(file.get(), false);
        if (Pair(info.nFileSizeHigh, info.nFileSizeLow) != entry.bytes) return false;
        FILE_DISPOSITION_INFO disposition{TRUE};
        return SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
    } catch (...) { return false; }
}
void Write(CacheLock const& cache, std::wstring const& name, Source const& source,
    iima::ThumbnailExtractionResult const& result, std::uint64_t maximum, std::stop_token stop) {
    Check(stop);
    iima::EncodedThumbnail frame{result.actualSeconds, result.jpeg};
    auto encoded = iima::EncodeThumbnailCache(source.legacy, std::span(&frame, 1), Limits());
    if (!encoded || encoded.bytes.size() > maximum) return;
    std::uint64_t total{};
    auto entries = Enumerate(cache, total);
    if (total > maximum - encoded.bytes.size()) {
        // Match upstream's half-budget reclaim but also require enough space
        // for this item, rather than writing past the configured cap.
        auto target = (std::max)(maximum / 2, total - (maximum - encoded.bytes.size()));
        std::uint64_t reclaimed{};
        for (auto const& entry : entries) {
            Check(stop);
            if (reclaimed >= target) break;
            if (Remove(cache, entry)) { reclaimed += entry.bytes; total -= entry.bytes; }
        }
    }
    if (total > maximum - encoded.bytes.size()) return;
    GUID id{};
    winrt::check_hresult(CoCreateGuid(&id));
    wchar_t text[40]{};
    if (!StringFromGUID2(id, text, 40)) throw winrt::hresult_error(E_FAIL);
    auto temporary = cache.directory.path / (std::wstring(L".write-") + text + L".tmp");
    auto raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    winrt::handle output{raw};
    struct Cleanup {
        std::filesystem::path value; winrt::handle& output;
        ~Cleanup() { output.close(); DeleteFileW(value.c_str()); }
    } cleanup{temporary, output};
    Information(output.get(), false);
    DWORD written{};
    winrt::check_bool(WriteFile(output.get(), encoded.bytes.data(), static_cast<DWORD>(encoded.bytes.size()), &written, nullptr));
    if (written != encoded.bytes.size()) throw winrt::hresult_error(E_FAIL);
    winrt::check_bool(FlushFileBuffers(output.get()));
    output.close();
    Check(stop);
    // Same-volume replacement exposes either the previous complete record or
    // the new complete record. Readers never observe the temporary write.
    winrt::check_bool(MoveFileExW(temporary.c_str(), (cache.directory.path / name).c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}
iima::ThumbnailExtractionResult CancelledResult(iima::ThumbnailRequest const& request) {
    iima::ThumbnailExtractionResult result;
    result.status = iima::ThumbnailExtractionStatus::Cancelled;
    result.generation = request.generation;
    result.requestedSeconds = request.seconds;
    return result;
}
}
namespace iima {
ThumbnailExtractionResult ExtractThumbnailCached(ThumbnailRequest const& request,
    std::int64_t maxCacheMebibytes, std::stop_token stop) {
    const auto epoch = cacheEpoch.load();
    if (stop.stop_requested()) return CancelledResult(request);
    auto maximum = ThumbnailCacheManager::BytesFromMebibytes(maxCacheMebibytes);
    if (!maximum || !*maximum || !std::isfinite(request.seconds) || request.seconds < 0 ||
        !request.maximumWidth || !request.maximumHeight || request.maximumWidth > 1024 || request.maximumHeight > 1024)
        return ExtractThumbnail(request, stop);
    std::optional<Source> source;
    std::wstring name;
    try {
        source.emplace(OpenSource(request.path));
        name = CacheName(*source, request);
        CacheLock cache(stop);
        if (auto hit = Read(cache, name, *source, request, stop)) return *hit;
    } catch (Cancelled const&) { return CancelledResult(request); }
      catch (...) { /* cache is optional; a failed identity must not be reused */ }
    if (stop.stop_requested()) return CancelledResult(request);
    auto result = ExtractThumbnail(request, stop);
    if (stop.stop_requested()) return CancelledResult(request);
    if (result.status == ThumbnailExtractionStatus::Ready && source && !name.empty()) {
        try {
            auto now = Information(source->file.get(), false);
            if (Pair(now.nFileSizeHigh, now.nFileSizeLow) != source->legacy.size ||
                Ticks(now.ftLastWriteTime) != Ticks(source->info.ftLastWriteTime)) return result;
            CacheLock cache(stop);
            if (epoch == cacheEpoch.load()) Write(cache, name, *source, result, *maximum, stop);
        } catch (Cancelled const&) { return CancelledResult(request); }
          catch (...) { /* disk pressure/corruption never hides a decoded image */ }
    }
    return stop.stop_requested() ? CancelledResult(request) : result;
}
ThumbnailDiskCacheClearResult ClearThumbnailDiskCache() {
    ThumbnailDiskCacheClearResult result;
    try {
        CacheLock cache({});
        ++cacheEpoch; // old background jobs in every window may not repopulate
        std::uint64_t ignored{};
        for (auto const& entry : Enumerate(cache, ignored)) {
            if (Remove(cache, entry)) ++result.deletedFiles;
            else ++result.failedFiles;
        }
    } catch (winrt::hresult_error const& error) { result.error = error.message().c_str(); }
      catch (...) { result.error = L"Unable to clear the thumbnail cache"; }
    return result;
}
}
