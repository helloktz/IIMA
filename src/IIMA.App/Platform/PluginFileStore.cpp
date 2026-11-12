// IINA JavascriptAPIFile.swift read/write/exists/list/delete,
// JavascriptAPIHttp.download and JavascriptAPI.parsePath.
// Copyright (c) 2020 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginFileStore.h"
#include "../../IIMA.Core/Plugins/PluginManifest.h"
#include <winrt/Windows.Data.Json.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <stdexcept>
#include <vector>
#include <algorithm>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Data::Json;
constexpr std::uint64_t MaxFile = 1024 * 1024;
constexpr std::uint64_t MaxTotal = 16 * MaxFile;
constexpr std::size_t MaxFiles = 128;
struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (FAILED(status) && status != RPC_E_CHANGED_MODE) check_hresult(status); }
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
std::wstring Wide(std::string_view bytes) {
    if (bytes.empty()) return {};
    auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("Plugin file text must be valid UTF-8.");
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    check_bool(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), out.data(), size));
    return out;
}
std::string Utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::invalid_argument("Plugin file text contains invalid UTF-16.");
    std::string out(static_cast<std::size_t>(size), '\0');
    check_bool(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr));
    return out;
}
template<std::size_t N> std::wstring Hex(std::array<unsigned char, N> const& bytes) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring out; out.reserve(N * 2);
    for (auto b : bytes) { out += digits[b >> 4]; out += digits[b & 15]; }
    return out;
}
std::wstring RandomName() {
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot generate plugin storage namespace.");
    return Hex(bytes);
}
void Check(HANDLE h, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{}; check_bool(GetFileInformationByHandle(h, &info));
    if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) ||
        (!directory && info.nNumberOfLinks != 1))
        throw std::runtime_error("Plugin storage requires ordinary local files with no links.");
}
std::uint64_t Length(HANDLE h) {
    LARGE_INTEGER size{}; check_bool(GetFileSizeEx(h, &size));
    if (size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(MaxFile))
        throw std::runtime_error("Plugin file exceeds 1 MiB.");
    return static_cast<std::uint64_t>(size.QuadPart);
}
void ValidateName(std::wstring_view name) {
    if (name.empty() || name.size() > 120 || name.back() == L'.' || name.back() == L' ' ||
        name == L"." || name == L".." || name.find_first_of(L"\\/:*?\"<>|") != name.npos ||
        std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; }))
        throw std::invalid_argument("Use one ordinary filename directly below @data/ or @tmp/.");
    std::wstring upper(name);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c) { return c >= L'a' && c <= L'z' ? c - 32 : c; });
    if (upper.starts_with(L".IIMA")) throw std::invalid_argument("Reserved plugin storage name.");
    auto base = upper.substr(0, upper.find(L'.'));
    while (!base.empty() && base.back() == L' ') base.pop_back();
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
        base == L"CONIN$" || base == L"CONOUT$" ||
        ((base.starts_with(L"COM") || base.starts_with(L"LPT")) && base.size() == 4 &&
         ((base[3] >= L'0' && base[3] <= L'9') || base[3] == L'\u00b9' || base[3] == L'\u00b2' || base[3] == L'\u00b3')))
        throw std::invalid_argument("Reserved Windows filename.");
}
struct DownloadPath { bool temporary; std::wstring name; };
DownloadPath ParseDownloadPath(std::string const& path) {
    if (path.empty() || path.size() > 512)
        throw std::invalid_argument("Plugin download path exceeds its limit.");
    auto wide = Wide(path);
    bool temporary = wide.starts_with(L"@tmp/");
    if (!temporary && !wide.starts_with(L"@data/"))
        throw std::invalid_argument("Only @tmp/ and @data/ download destinations are available.");
    auto name = wide.substr(temporary ? 5 : 6);
    ValidateName(name); // actual filesystem link checks remain in Directory/Open
    return {temporary, std::move(name)};
}
struct Directory {
    std::filesystem::path path;
    std::vector<handle> ancestors;
    Directory(std::wstring const& identity, bool temporary, std::wstring const& session, bool create = true) {
        PWSTR raw{}; check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw));
        struct Free { PWSTR p; ~Free() { CoTaskMemFree(p); } } free{raw};
        path = std::filesystem::path(raw) / L"IIMA" / L"PluginFiles-v1" / identity /
            (temporary ? L"tmp" : L"data");
        if (temporary) path /= session;
        auto native = path.native();
        if (native.size() > 220 || native.size() < 3 || native[1] != L':' || native[2] != L'\\' || native.find(L':', 2) != native.npos)
            throw std::runtime_error("Plugin storage requires a short local drive path.");
        auto root = path.root_path(); auto drive = GetDriveTypeW(root.c_str());
        if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE) throw std::runtime_error("Network plugin storage is not supported.");
        auto hold = [&](std::filesystem::path const& p, bool create) {
            if (create && !CreateDirectoryW(p.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) throw_last_error();
            auto rawHandle = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (rawHandle == INVALID_HANDLE_VALUE) throw_last_error();
            handle h{rawHandle}; Check(h.get(), true); ancestors.push_back(std::move(h));
        };
        hold(root, false);
        for (auto const& component : path.relative_path()) {
            auto part = component.native();
            if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
                throw std::runtime_error("Ambiguous storage path.");
            root /= component; hold(root, create);
        }
    }
};
handle Lock(Directory const& dir) {
    auto path = dir.path / L".iima.lock";
    auto until = GetTickCount64() + 1000;
    for (;;) {
        auto raw = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (raw != INVALID_HANDLE_VALUE) { handle h{raw}; Check(h.get(), false); return h; }
        auto error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION || GetTickCount64() >= until)
            throw hresult_error(HRESULT_FROM_WIN32(error), L"Plugin file storage is busy or unavailable.");
        Sleep(10);
    }
}
handle Open(std::filesystem::path const& path, DWORD access, bool missingOkay = false) {
    auto raw = CreateFileW(path.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        auto error = GetLastError();
        if (missingOkay && error == ERROR_FILE_NOT_FOUND) return {};
        throw hresult_error(HRESULT_FROM_WIN32(error));
    }
    handle h{raw}; Check(h.get(), false); return h;
}
struct Entry { std::wstring name; std::uint64_t size{}; };
std::vector<Entry> Scan(Directory const& dir) {
    WIN32_FIND_DATAW data{};
    auto raw = FindFirstFileW((dir.path / L"*").c_str(), &data);
    if (raw == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        throw_last_error();
    }
    struct Search { HANDLE h; ~Search() { FindClose(h); } } search{raw};
    std::vector<Entry> entries; std::uint64_t total{};
    do {
        std::wstring name(data.cFileName);
        if (name == L"." || name == L".." || name == L".iima.lock") continue;
        if (entries.size() >= MaxFiles) throw std::runtime_error("Plugin storage file-count limit exceeded.");
        auto file = Open(dir.path / name, FILE_READ_ATTRIBUTES);
        auto size = Length(file.get()); total += size;
        if (total > MaxTotal) throw std::runtime_error("Plugin storage quota exceeded.");
        entries.push_back({std::move(name), size});
    } while (FindNextFileW(raw, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw_last_error();
    std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) { return a.name < b.name; });
    return entries;
}
void CheckCancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("Plugin download cancelled.");
}
void WriteAtomic(std::filesystem::path const& path, std::string_view content,
    std::stop_token stop = {}) {
    CheckCancelled(stop);
    auto temporary = path.parent_path() / (L".iima-" + RandomName());
    auto raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) throw_last_error();
    handle h{raw};
    try {
        Check(h.get(), false);
        std::size_t offset{};
        while (offset < content.size()) {
            CheckCancelled(stop);
            auto size = static_cast<DWORD>(std::min<std::size_t>(65536, content.size() - offset));
            DWORD done{};
            check_bool(WriteFile(h.get(), content.data() + offset, size, &done, nullptr));
            if (done != size) throw std::runtime_error("Incomplete plugin file write.");
            offset += done;
        }
        CheckCancelled(stop);
        check_bool(FlushFileBuffers(h.get())); h.close();
        CheckCancelled(stop); // last reversible point: no cancellation error after rename
        check_bool(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    } catch (...) { h.close(); DeleteFileW(temporary.c_str()); throw; }
}
void StoreBytes(Directory const& directory, std::filesystem::path const& target,
    std::string_view content, std::stop_token stop = {}) {
    CheckCancelled(stop);
    if (content.size() > MaxFile) throw std::invalid_argument("Plugin file exceeds 1 MiB.");
    auto existing = Open(target, FILE_READ_ATTRIBUTES, true);
    auto oldSize = existing ? Length(existing.get()) : 0;
    auto entries = Scan(directory); std::uint64_t total{};
    for (auto const& entry : entries) total += entry.size;
    if (total < oldSize || (!existing && entries.size() >= MaxFiles) || total - oldSize + content.size() > MaxTotal)
        throw std::runtime_error("Plugin storage quota exceeded or changed externally.");
    existing.close(); // all cooperating API writers hold the directory lock
    WriteAtomic(target, content, stop);
}
}
PluginFileStore::PluginFileStore(std::string identifier) {
    if (!IsPluginIdentifier(identifier)) throw std::invalid_argument("Invalid plugin identifier.");
    std::array<unsigned char, 32> bytes{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(identifier.data()),
        static_cast<ULONG>(identifier.size()), bytes.data(), static_cast<ULONG>(bytes.size())) < 0)
        throw std::runtime_error("Cannot derive plugin file namespace.");
    identity_ = Hex(bytes); temporary_ = RandomName();
}
std::string PluginFileStore::Invoke(std::string const& operation, std::string const& argumentsJson) {
    std::lock_guard guard(mutex_);
    if (closed_) throw std::runtime_error("Plugin storage is closed.");
    Apartment apartment;
    if (operation != "read" && operation != "write" && operation != "exists" && operation != "list" && operation != "delete")
        throw std::invalid_argument("Unsupported plugin file operation.");
    if (argumentsJson.size() > 8 * MaxFile) throw std::invalid_argument("Plugin file arguments are too large.");
    // This bridge accepts a flat argument object plus one options object; reject deep nesting before platform JSON
    // parsing so a malicious script cannot allocate an unbounded parser stack.
    bool quoted{}, escape{}; unsigned depth{};
    for (auto c : argumentsJson) {
        if (quoted) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 2) throw std::invalid_argument("File arguments exceed the nesting limit."); }
        else if (c == '}' || c == ']') { if (!depth) throw std::invalid_argument("Invalid file arguments."); --depth; }
    }
    auto args = JsonObject::Parse(Wide(argumentsJson));
    auto path = std::wstring(args.GetNamedString(L"path"));
    bool temporary = path.starts_with(L"@tmp/");
    if (!temporary && !path.starts_with(L"@data/")) throw std::invalid_argument("Only @tmp/ and @data/ paths are available.");
    auto name = path.substr(temporary ? 5 : 6);
    if (!name.empty()) ValidateName(name);
    if (name.empty() && operation != "list" && operation != "exists") throw std::invalid_argument("A filename is required.");
    auto options = args.HasKey(L"options") ? args.GetNamedObject(L"options") : JsonObject{};
    if (options.HasKey(L"encoding")) {
        auto encoding = options.GetNamedString(L"encoding");
        if (encoding != L"utf8" && encoding != L"utf-8" && encoding != L"UTF-8") throw std::invalid_argument("Only UTF-8 encoding is supported.");
    }
    if (options.HasKey(L"includeSubDir") && options.GetNamedBoolean(L"includeSubDir"))
        throw std::invalid_argument("Recursive plugin file listing is not supported.");
    Directory directory(identity_, temporary, temporary_);
    if (temporary) temporaryCreated_ = true;
    auto lock = Lock(directory);
    auto target = directory.path / name;
    if (target.native().size() > 240) throw std::invalid_argument("Plugin storage path is too long.");
    if (operation == "list") {
        if (!name.empty()) throw std::invalid_argument("List accepts @data/ or @tmp/ only.");
        JsonArray result;
        for (auto const& entry : Scan(directory)) {
            if (entry.name.starts_with(L".iima-")) continue; // failed atomic-write remnants are not API files
            JsonObject item;
            item.Insert(L"filename", JsonValue::CreateStringValue(entry.name));
            item.Insert(L"path", JsonValue::CreateStringValue(L"/" + entry.name));
            item.Insert(L"isDir", JsonValue::CreateBooleanValue(false)); result.Append(item);
        }
        return to_string(result.Stringify());
    }
    if (operation == "exists") {
        if (name.empty()) return "true";
        auto file = Open(target, FILE_READ_ATTRIBUTES, true); return file ? "true" : "false";
    }
    if (operation == "read") {
        auto file = Open(target, GENERIC_READ); auto size = Length(file.get());
        std::string content(static_cast<std::size_t>(size), '\0'); DWORD got{};
        check_bool(ReadFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &got, nullptr));
        if (got != content.size()) throw std::runtime_error("Incomplete plugin file read.");
        return to_string(JsonValue::CreateStringValue(Wide(content)).Stringify());
    }
    if (operation == "delete") {
        auto file = Open(target, DELETE);
        FILE_DISPOSITION_INFO disposition{TRUE};
        check_bool(SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition)));
        return "null";
    }
    auto content = Utf8(args.GetNamedString(L"content"));
    StoreBytes(directory, target, content);
    return "null";
}
void PluginFileStore::ValidateDownloadPath(std::string const& path) {
    (void)ParseDownloadPath(path);
}
void PluginFileStore::WriteDownload(std::string const& path, std::vector<std::uint8_t> const& bytes,
    std::stop_token stop) {
    CheckCancelled(stop);
    if (bytes.size() > MaxFile) throw std::invalid_argument("Plugin download exceeds 1 MiB.");
    auto [temporary, name] = ParseDownloadPath(path);
    std::lock_guard guard(mutex_);
    CheckCancelled(stop);
    if (closed_) throw std::runtime_error("Plugin storage is closed.");
    Apartment apartment;
    Directory directory(identity_, temporary, temporary_);
    if (temporary) temporaryCreated_ = true;
    auto lock = Lock(directory);
    CheckCancelled(stop);
    auto target = directory.path / name;
    if (target.native().size() > 240) throw std::invalid_argument("Plugin storage path is too long.");
    auto data = bytes.empty() ? "" : reinterpret_cast<char const*>(bytes.data());
    StoreBytes(directory, target, std::string_view(data, bytes.size()), stop);
}
void PluginFileStore::CleanupTemporary() noexcept {
    try {
        std::lock_guard guard(mutex_);
        closed_ = true;
        if (!temporaryCreated_) return;
        temporaryCreated_ = false;
        Directory directory(identity_, true, temporary_, false);
        auto lock = Lock(directory);
        WIN32_FIND_DATAW data{};
        auto raw = FindFirstFileW((directory.path / L"*").c_str(), &data);
        if (raw == INVALID_HANDLE_VALUE) return;
        struct Search { HANDLE h; ~Search() { FindClose(h); } } search{raw};
        std::size_t visited{};
        do {
            std::wstring name(data.cFileName);
            if (name == L"." || name == L".." || name == L".iima.lock") continue;
            // No recursive removal; retain any unusual entry instead of risking
            // another owner's files. Count all entries to bound cleanup work.
            if (++visited > MaxFiles + 16) break;
            try {
                auto file = Open(directory.path / name, DELETE);
                FILE_DISPOSITION_INFO disposition{TRUE};
                check_bool(SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition)));
            } catch (...) { /* preserve locked, linked or otherwise unsafe file */ }
        } while (FindNextFileW(raw, &data));
        // Keep the empty namespace/lock file: removing a pathname after closing
        // its ancestor handles would introduce a substitution race. A future
        // explicit cache-maintenance UI may reclaim these directories safely.
    } catch (...) { /* shutdown cleanup is best effort; never masks script result */ }
}

}
