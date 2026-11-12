// Windows temporary-file adaptation of IINA screenshotCacheURL/callback.
// Source logic: IINA contributors, GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "ScreenshotStaging.h"
#include <objbase.h>
#include <utility>
#include <algorithm>
#include <vector>

namespace {
std::wstring UniqueName() {
    GUID guid{};
    winrt::check_hresult(CoCreateGuid(&guid));
    wchar_t text[40]{};
    if (!StringFromGUID2(guid, text, static_cast<int>(std::size(text))))
        throw winrt::hresult_error(E_FAIL, L"Cannot create screenshot file name");
    return std::wstring(L"IIMA-screenshot-") + text;
}
// Hold ancestors without write/delete sharing through publication. This prevents a
// directory/junction swap between validation and path-based copy/rename.
std::vector<winrt::handle> LockDirectory(std::filesystem::path const& directory) {
    auto text=directory.native();
    std::replace(text.begin(),text.end(),L'/',L'\x5c');
    if (text.empty() || text.size()>32000 || text.find(L'\0')!=text.npos ||
        !directory.is_absolute() || text.starts_with(LR"(\\?\)") || text.starts_with(LR"(\\.\)") ||
        text.find(L':',2)!=text.npos || std::any_of(text.begin(),text.end(),[](wchar_t c) { return c<32 || c==127; }))
        throw winrt::hresult_invalid_argument(L"Invalid screenshot directory");
    for (auto const& component:directory.relative_path()) {
        auto value=component.native();
        if (value==L"." || value==L".." || (!value.empty() && (value.back()==L'.' || value.back()==L' ')))
            throw winrt::hresult_invalid_argument(L"Ambiguous screenshot directory");
    }
    std::vector<std::filesystem::path> ancestors;
    for (auto path=directory;!path.empty();) {
        // A UNC server alone is not a directory handle; validate its share.
        if (path==path.root_path() && path.native().starts_with(LR"(\\)")) break;
        if (ancestors.size()>=256) throw winrt::hresult_invalid_argument(L"Screenshot directory is too deep");
        ancestors.push_back(path);
        auto parent=path.parent_path();
        if (parent==path) break;
        path=std::move(parent);
    }
    if (ancestors.empty()) throw winrt::hresult_invalid_argument(L"Screenshot directory requires a share or volume");
    std::vector<winrt::handle> locks;
    for (auto it=ancestors.rbegin();it!=ancestors.rend();++it) {
        auto raw=CreateFileW(it->c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (raw==INVALID_HANDLE_VALUE) winrt::throw_last_error();
        winrt::handle handle(raw);
        FILE_ATTRIBUTE_TAG_INFO info{};
        winrt::check_bool(GetFileInformationByHandleEx(handle.get(),FileAttributeTagInfo,&info,sizeof(info)));
        if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)))
            throw winrt::hresult_invalid_argument(L"Screenshot directory cannot traverse links");
        locks.push_back(std::move(handle));
    }
    return locks;
}
bool SafeLeaf(std::wstring_view leaf) {
    if (leaf.empty() || leaf.size()>240 || leaf==L"." || leaf==L".." || leaf.back()==L'.' || leaf.back()==L' ') return false;
    for (auto c:leaf) if (c<32 || c==127 || std::wstring_view(LR"(<>:"/\|?*)").find(c)!=std::wstring_view::npos) return false;
    auto dot=leaf.find(L'.');
    std::wstring device(leaf.substr(0,dot));
    while (!device.empty() && device.back()==L' ') device.pop_back();
    for (auto& c:device) if (c>=L'a' && c<=L'z') c-=L'a'-L'A';
    if (device==L"CON" || device==L"PRN" || device==L"AUX" || device==L"NUL" || device==L"CONIN$" || device==L"CONOUT$") return false;
    if (device.size()==4 && (device.starts_with(L"COM") || device.starts_with(L"LPT")) &&
        ((device[3]>=L'1' && device[3]<=L'9') || device[3]==L'\u00b9' || device[3]==L'\u00b2' || device[3]==L'\u00b3')) return false;
    return true;
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
void ScreenshotStaging::ValidateDirectory(const std::filesystem::path& directory) {
    const auto locks=LockDirectory(directory);
}
std::filesystem::path ScreenshotStaging::PublishUnique(const std::filesystem::path& directory,
    std::wstring_view leaf) const {
    if (path_.empty() || !SafeLeaf(leaf)) throw winrt::hresult_invalid_argument(L"Invalid screenshot filename");
    const auto locks=LockDirectory(directory);
    const auto temporary=directory/(UniqueName()+L".tmp");
    winrt::check_bool(CopyFileW(path_.c_str(),temporary.c_str(),TRUE));
    try {
        auto raw=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (raw==INVALID_HANDLE_VALUE) winrt::throw_last_error();
        winrt::handle file(raw);
        FILE_ATTRIBUTE_TAG_INFO info{};
        winrt::check_bool(GetFileInformationByHandleEx(file.get(),FileAttributeTagInfo,&info,sizeof(info)));
        if (info.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))
            throw winrt::hresult_invalid_argument(L"Invalid screenshot temporary file");
        winrt::check_bool(FlushFileBuffers(file.get()));
        file.close();
        const std::filesystem::path requested{std::wstring(leaf)};
        for (unsigned collision=0;collision<1000;++collision) {
            auto name=collision ? requested.stem().native()+L"-"+std::to_wstring(collision)+requested.extension().native() : requested.native();
            auto destination=directory/name;
            // No REPLACE_EXISTING: simultaneous windows cannot overwrite each
            // other. The staged copy is renamed within its destination volume.
            if (MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)) return destination;
            const auto error=GetLastError();
            if (error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS) winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        }
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_EXISTS),L"Screenshot name collision limit reached");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}

}
