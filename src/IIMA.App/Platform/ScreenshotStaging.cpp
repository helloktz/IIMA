// Windows temporary-file adaptation of IINA screenshotCacheURL/callback.
// Source logic: IINA contributors, GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "ScreenshotStaging.h"
#include <objbase.h>
#include <utility>

namespace {
std::wstring UniqueName() {
    GUID guid{};
    winrt::check_hresult(CoCreateGuid(&guid));
    wchar_t text[40]{};
    if (!StringFromGUID2(guid, text, static_cast<int>(std::size(text))))
        throw winrt::hresult_error(E_FAIL, L"Cannot create screenshot file name");
    return std::wstring(L"IIMA-screenshot-") + text;
}
}
namespace iima {
ScreenshotStaging::ScreenshotStaging(std::filesystem::path directory, std::filesystem::path path)
    : directory_(std::move(directory)), path_(std::move(path)) {}
ScreenshotStaging ScreenshotStaging::Create(std::wstring_view extension) {
    if (extension != L"png" && extension != L"jpg" && extension != L"jpeg" &&
        extension != L"webp" && extension != L"jxl" && extension != L"avif")
        throw winrt::hresult_invalid_argument(L"Unsupported screenshot extension");
    std::wstring temporary(32768, L'\0');
    auto length = GetTempPath2W(static_cast<DWORD>(temporary.size()), temporary.data());
    if (!length) winrt::throw_last_error();
    if (length >= temporary.size()) throw winrt::hresult_error(E_FAIL, L"Temporary path is too long");
    temporary.resize(length);
    const auto directory = std::filesystem::path(temporary) / UniqueName();
    // No directory reuse: do not follow an attacker-created link if a name
    // already exists. The caller's user temp ACL is inherited by the new dir.
    winrt::check_bool(CreateDirectoryW(directory.c_str(), nullptr));
    try {
        return ScreenshotStaging(directory, directory / (std::wstring(L"capture.") + std::wstring(extension)));
    } catch (...) { RemoveDirectoryW(directory.c_str()); throw; }
}
ScreenshotStaging::ScreenshotStaging(ScreenshotStaging&& other) noexcept
    : directory_(std::exchange(other.directory_, {})), path_(std::exchange(other.path_, {})) {}
ScreenshotStaging& ScreenshotStaging::operator=(ScreenshotStaging&& other) noexcept {
    if (this != &other) {
        Cleanup();
        directory_ = std::exchange(other.directory_, {});
        path_ = std::exchange(other.path_, {});
    }
    return *this;
}
ScreenshotStaging::~ScreenshotStaging() { Cleanup(); }
void ScreenshotStaging::Cleanup() noexcept {
    // Delete only the exact owned file and empty directory. Never recurse or
    // remove arbitrary neighboring files when cleanup encounters interference.
    if (!path_.empty()) DeleteFileW(path_.c_str());
    if (!directory_.empty()) RemoveDirectoryW(directory_.c_str());
    path_.clear(); directory_.clear();
}
const std::filesystem::path& ScreenshotStaging::Path() const noexcept { return path_; }
void ScreenshotStaging::Publish(const std::filesystem::path& destination) const {
    if (path_.empty() || destination.empty() || destination.filename().empty())
        throw winrt::hresult_invalid_argument(L"Invalid screenshot destination");
    auto temporary = destination.parent_path() / (UniqueName() + L".tmp");
    winrt::check_bool(CopyFileW(path_.c_str(), temporary.c_str(), TRUE));
    try {
        auto raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
        winrt::handle file(raw);
        winrt::check_bool(FlushFileBuffers(file.get()));
        file.close();
        winrt::check_bool(MoveFileExW(temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
}
