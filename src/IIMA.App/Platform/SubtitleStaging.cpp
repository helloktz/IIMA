// Downloaded subtitle ownership adapted from IINA OnlineSubtitle/AssrtSubtitle.
// Copyright (c) 2017-2018 lhc and IINA contributors. GPL-3.0.
// Source snapshot and license: references/iina-source.txt, references/iina/LICENSE.
#include "pch.h"
#include "SubtitleStaging.h"
#include <algorithm>
#include <array>
#include <objbase.h>
#include <initializer_list>
#include <string>
#include <utility>

namespace {
std::wstring UniqueDirectoryName() {
    GUID value{};
    winrt::check_hresult(CoCreateGuid(&value));
    wchar_t text[40]{};
    if (!StringFromGUID2(value, text, static_cast<int>(std::size(text))))
        throw winrt::hresult_error(E_FAIL, L"Cannot create subtitle directory name");
    return std::wstring(L"IIMA-subtitles-") + text;
}

std::wstring DecodeFilename(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > 4096 || utf8.find('\0') != utf8.npos)
        throw winrt::hresult_invalid_argument(L"Invalid subtitle filename");
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (!count) throw winrt::hresult_invalid_argument(L"Subtitle filename is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), result.data(), count)) winrt::throw_last_error();
    return result;
}

std::wstring SafeName(std::string_view utf8) {
    auto name = DecodeFilename(utf8);
    // Flatten either platform's separator. Never append a provider path directly.
    if (const auto at = name.find_last_of(L"/\\"); at != name.npos) name.erase(0, at + 1);
    if (name.empty() || name.find(L':') != name.npos)
        throw winrt::hresult_invalid_argument(L"Subtitle filename cannot contain a stream name");
    const auto dot = name.find_last_of(L'.');
    if (dot == name.npos || dot + 1 == name.size())
        throw winrt::hresult_invalid_argument(L"Subtitle file has no supported extension");
    auto extension = name.substr(dot);
    for (auto& ch : extension) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
    constexpr std::array<std::wstring_view, 8> supported{
        L".srt", L".ass", L".ssa", L".vtt", L".lrc", L".smi", L".sami", L".sub"};
    if (std::find(supported.begin(), supported.end(), extension) == supported.end())
        throw winrt::hresult_invalid_argument(L"Only plain-text subtitle downloads are supported");
    auto stem = name.substr(0, dot);
    for (auto& ch : stem) {
        // Remove display direction controls, control chars and forbidden Win32
        // name characters. Extension is strict and cannot be obscured by them.
        if (ch < 0x20 || ch == 0x7f || (ch >= 0x202a && ch <= 0x202e) ||
            (ch >= 0x2066 && ch <= 0x2069) || ch == L'<' || ch == L'>' ||
            ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*') ch = L'_';
    }
    while (!stem.empty() && (stem.back() == L'.' || stem.back() == L' ')) stem.pop_back();
    // Leave margin for the unique sequence prefix and ordinary MAX_PATH temp
    // roots. Avoid splitting a UTF-16 surrogate pair at the truncation boundary.
    if (stem.size() > 80) {
        stem.resize(80);
        if (stem.back() >= 0xd800 && stem.back() <= 0xdbff) stem.pop_back();
    }
    if (stem.empty()) stem = L"subtitle";
    // All actual names begin with a numeric prefix, so CON, NUL, COM1 and other
    // DOS device stems can never be used as the complete Win32 basename.
    return stem + extension;
}

void RequireText(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > iima::SubtitleStaging::MaximumFileBytes)
        throw winrt::hresult_invalid_argument(L"Subtitle download is empty or exceeds 8 MiB");
    const auto starts = [&](std::initializer_list<std::uint8_t> signature) {
        return bytes.size() >= signature.size() && std::equal(signature.begin(), signature.end(), bytes.begin());
    };
    if (starts({'M', 'Z'}) || starts({'P', 'K', 3, 4}) || starts({'P', 'K', 5, 6}) ||
        starts({'P', 'K', 7, 8}) || starts({'R', 'a', 'r', '!', 0x1a, 7}) ||
        starts({0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c}) || starts({0x1f, 0x8b}) ||
        starts({0x7f, 'E', 'L', 'F'}))
        throw winrt::hresult_invalid_argument(L"Compressed or executable subtitle downloads are not supported");
    // Preserve legacy non-UTF-8 single-byte subtitles: mpv owns encoding choice.
    // UTF-16 BOM is allowed because legitimate text contains zero octets. We do
    // not guess BOM-less UTF-16, UTF-32, VobSub or PGS from a misleading extension.
    if (starts({0xff, 0xfe}) || starts({0xfe, 0xff})) {
        if (bytes.size() <= 2 || bytes.size() % 2 != 0 || starts({0xff, 0xfe, 0, 0}))
            throw winrt::hresult_invalid_argument(L"Unsupported subtitle text encoding");
        const bool little = bytes[0] == 0xff;
        for (std::size_t i = 2; i < bytes.size(); i += 2) {
            const auto value = little ? (bytes[i] | (bytes[i + 1] << 8)) :
                ((bytes[i] << 8) | bytes[i + 1]);
            if (value == 0) throw winrt::hresult_invalid_argument(L"Subtitle contains binary data");
        }
    } else if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end()) {
        throw winrt::hresult_invalid_argument(L"Subtitle contains binary data");
    }
    // This is a bounded format guard, not a subtitle parser or malware scanner.
}
}

namespace iima {
SubtitleStaging::SubtitleStaging(std::filesystem::path directory, winrt::handle directoryLock)
    : directory_(std::move(directory)), directoryLock_(std::move(directoryLock)) {}

SubtitleStaging SubtitleStaging::Create() {
    std::wstring temporary(32768, L'\0');
    const auto count = GetTempPath2W(static_cast<DWORD>(temporary.size()), temporary.data());
    if (!count) winrt::throw_last_error();
    if (count >= temporary.size()) throw winrt::hresult_error(E_FAIL, L"Temporary path is too long");
    temporary.resize(count);
    auto directory = std::filesystem::path(temporary) / UniqueDirectoryName();
    // Inherit the user's temp ACL. Never reuse an existing directory or link.
    winrt::check_bool(CreateDirectoryW(directory.c_str(), nullptr));
    try {
        auto raw = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
        winrt::handle lock(raw);
        BY_HANDLE_FILE_INFORMATION information{};
        winrt::check_bool(GetFileInformationByHandle(lock.get(), &information));
        if (!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw winrt::hresult_error(E_ACCESSDENIED, L"Unsafe subtitle temporary directory");
        return SubtitleStaging(std::move(directory), std::move(lock));
    } catch (...) { RemoveDirectoryW(directory.c_str()); throw; }
}

SubtitleStaging::SubtitleStaging(SubtitleStaging&& other) noexcept
    : directory_(std::exchange(other.directory_, {})), directoryLock_(std::move(other.directoryLock_)),
      files_(std::move(other.files_)), totalBytes_(std::exchange(other.totalBytes_, 0)),
      nextFile_(std::exchange(other.nextFile_, 0)) { other.files_.clear(); }
SubtitleStaging& SubtitleStaging::operator=(SubtitleStaging&& other) noexcept {
    if (this != &other) {
        Cleanup();
        directory_ = std::exchange(other.directory_, {});
        directoryLock_ = std::move(other.directoryLock_);
        files_ = std::move(other.files_);
        other.files_.clear();
        totalBytes_ = std::exchange(other.totalBytes_, 0);
        nextFile_ = std::exchange(other.nextFile_, 0);
    }
    return *this;
}
SubtitleStaging::~SubtitleStaging() { Cleanup(); }
const std::filesystem::path& SubtitleStaging::Directory() const noexcept { return directory_; }

std::filesystem::path SubtitleStaging::Write(std::string_view filename,
    const std::vector<std::uint8_t>& bytes) {
    if (directory_.empty() || !directoryLock_)
        throw winrt::hresult_error(E_ILLEGAL_METHOD_CALL, L"Subtitle staging is not initialized");
    RequireText(bytes);
    if (files_.size() >= MaximumFiles || bytes.size() > MaximumTotalBytes - totalBytes_)
        throw winrt::hresult_error(E_OUTOFMEMORY, L"Subtitle download batch exceeds its limit");
    const auto basename = SafeName(filename);
    // A failed or duplicate request never overwrites an earlier subtitle.
    auto path = directory_ / (std::to_wstring(++nextFile_) + L"-" + basename);
    // Reserve bookkeeping first so no allocation failure after publication loses
    // ownership of a completed file. On any error this entry is removed.
    files_.push_back(path);
    auto raw = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        files_.pop_back();
        winrt::throw_hresult(HRESULT_FROM_WIN32(error));
    }
    try {
        winrt::handle file(raw);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written{};
            const auto length = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
            winrt::check_bool(WriteFile(file.get(), bytes.data() + offset, length, &written, nullptr));
            if (!written) throw winrt::hresult_error(E_FAIL, L"Subtitle file write made no progress");
            offset += written;
        }
        winrt::check_bool(FlushFileBuffers(file.get()));
        file.close();
        totalBytes_ += bytes.size();
        return path;
    } catch (...) {
        // handle destruction precedes this handler. Leave a failed cleanup path
        // tracked for one more best-effort deletion at owner destruction.
        if (DeleteFileW(path.c_str())) files_.pop_back();
        throw;
    }
}
void SubtitleStaging::Cleanup() noexcept {
    // Never recurse through a provider-controlled name, directory or junction.
    for (const auto& path : files_) DeleteFileW(path.c_str());
    files_.clear();
    directoryLock_.close();
    if (!directory_.empty()) RemoveDirectoryW(directory_.c_str());
    directory_.clear();
    totalBytes_ = 0;
    nextFile_ = 0;
}
}
