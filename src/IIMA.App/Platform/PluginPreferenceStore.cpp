// IINA JavascriptPlugin.preferences/defaultPreferences/syncPreferences adaptation.
// Copyright (c) 2019 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginPreferenceStore.h"
#include "../../IIMA.Core/Plugins/PluginManifest.h"
#include <winrt/Windows.Data.Json.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Data::Json;
constexpr std::size_t MaxBytes = 64 * 1024;
constexpr std::size_t MaxNodes = 4096;
constexpr unsigned MaxDepth = 16;
std::atomic<unsigned long> serial{};
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (FAILED(result) && result != RPC_E_CHANGED_MODE) check_hresult(result); }
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
void SyntaxBudget(std::string_view text) {
    if (text.empty() || text.size() > MaxBytes || text.find('\0') != text.npos)
        throw std::invalid_argument("Plugin preference JSON exceeds its size limit or contains NUL.");
    // Reject excessive parser nesting before invoking the platform JSON parser.
    // The parser remains authoritative for syntax and matching bracket types.
    bool quoted{}, escape{};
    unsigned depth{};
    for (char c : text) {
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > MaxDepth) throw std::invalid_argument("Plugin preference nesting limit exceeded.");
        } else if (c == '}' || c == ']') {
            if (!depth) throw std::invalid_argument("Invalid plugin preference JSON.");
            --depth;
        }
    }
    if (quoted || depth) throw std::invalid_argument("Incomplete plugin preference JSON.");
}
void Validate(IJsonValue const& value, unsigned depth, std::size_t& nodes) {
    if (++nodes > MaxNodes || depth > MaxDepth)
        throw std::invalid_argument("Plugin preference value budget exceeded.");
    switch (value.ValueType()) {
    case JsonValueType::Object:
        for (auto const& entry : value.GetObject()) {
            auto key = entry.Key();
            if (key.empty() || key.size() > 256 || std::wstring_view(key).find(L'\0') != std::wstring_view::npos)
                throw std::invalid_argument("Plugin preference keys must contain 1 to 256 non-NUL characters.");
            Validate(entry.Value(), depth + 1, nodes);
        }
        break;
    case JsonValueType::Array:
        for (auto const& entry : value.GetArray()) Validate(entry, depth + 1, nodes);
        break;
    case JsonValueType::Number:
        if (!std::isfinite(value.GetNumber())) throw std::invalid_argument("Non-finite preference number.");
        break;
    case JsonValueType::String:
    case JsonValueType::Boolean:
    case JsonValueType::Null:
        break;
    default: throw std::invalid_argument("Unsupported plugin preference value.");
    }
}
JsonObject Parse(std::string_view text) {
    SyntaxBudget(text);
    // CP_UTF8 strict conversion rejects invalid UTF-8 instead of normalizing
    // malformed bytes into a different preference key/value.
    auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) throw_last_error();
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length))
        throw_last_error();
    auto object = JsonObject::Parse(wide);
    std::size_t nodes{};
    Validate(object, 0, nodes);
    if (to_string(object.Stringify()).size() > MaxBytes)
        throw std::invalid_argument("Serialized plugin preferences exceed 64 KiB.");
    return object;
}
bool Equal(IJsonValue const& a, IJsonValue const& b) {
    if (a.ValueType() != b.ValueType()) return false;
    switch (a.ValueType()) {
    case JsonValueType::Object: {
        auto left = a.GetObject(), right = b.GetObject();
        if (left.Size() != right.Size()) return false;
        for (auto const& item : left)
            if (!right.HasKey(item.Key()) || !Equal(item.Value(), right.Lookup(item.Key()))) return false;
        return true;
    }
    case JsonValueType::Array: {
        auto left = a.GetArray(), right = b.GetArray();
        if (left.Size() != right.Size()) return false;
        for (std::uint32_t i = 0; i < left.Size(); ++i) if (!Equal(left.GetAt(i), right.GetAt(i))) return false;
        return true;
    }
    case JsonValueType::String: return a.GetString() == b.GetString();
    case JsonValueType::Number: return a.GetNumber() == b.GetNumber();
    case JsonValueType::Boolean: return a.GetBoolean() == b.GetBoolean();
    case JsonValueType::Null: return true;
    default: return false;
    }
}
void CheckFile(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    check_bool(GetFileInformationByHandle(handle, &info));
    if (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) ||
        (!directory && info.nNumberOfLinks != 1))
        throw hresult_error(E_ACCESSDENIED, L"Plugin preferences require ordinary local files and directories.");
}
struct Directory {
    std::filesystem::path path;
    std::vector<handle> held;
    Directory() {
        PWSTR raw{};
        check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw));
        struct Free { PWSTR value; ~Free() { CoTaskMemFree(value); } } free{raw};
        path = std::filesystem::path(raw) / L"IIMA" / L"PluginPreferences-v1";
        auto text = path.native();
        if (text.size() < 3 || text[1] != L':' || text[2] != L'\\' || text.find(L':', 2) != text.npos)
            throw hresult_error(E_ACCESSDENIED, L"Plugin preferences require a local drive.");
        auto root = path.root_path();
        auto drive = GetDriveTypeW(root.c_str());
        if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE)
            throw hresult_error(E_ACCESSDENIED, L"Network plugin preference storage is not supported.");
        auto hold = [&](std::filesystem::path const& p, bool create) {
            if (create && !CreateDirectoryW(p.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) throw_last_error();
            auto value = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (value == INVALID_HANDLE_VALUE) throw_last_error();
            handle file{value}; CheckFile(file.get(), true); held.push_back(std::move(file));
        };
        hold(root, false);
        for (auto const& component : path.relative_path()) {
            auto part = component.native();
            if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
                throw hresult_error(E_ACCESSDENIED, L"Ambiguous plugin preference directory.");
            root /= component; hold(root, true);
        }
    }
};
handle Lock(Directory const& dir) {
    // An exclusive ordinary file handle serializes all windows and processes;
    // crash cleanup is automatic. It is never removed and never stores data.
    auto path = dir.path / L"store.lock";
    auto end = GetTickCount64() + 2000;
    for (;;) {
        auto raw = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (raw != INVALID_HANDLE_VALUE) { handle file{raw}; CheckFile(file.get(), false); return file; }
        auto error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) || GetTickCount64() >= end)
            throw hresult_error(HRESULT_FROM_WIN32(error), L"Plugin preference storage is busy or unavailable.");
        Sleep(20);
    }
}
std::wstring Filename(std::string_view identifier) {
    if (!IsPluginIdentifier(identifier)) throw std::invalid_argument("Invalid plugin preference identifier.");
    // SHA-256 preserves exact, case-sensitive identity on case-insensitive NTFS,
    // avoids reserved filenames, and keeps all names below the component limit.
    std::array<unsigned char, 32> bytes{};
    auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(identifier.data())), static_cast<ULONG>(identifier.size()),
        bytes.data(), static_cast<ULONG>(bytes.size()));
    if (status < 0) throw hresult_error(E_FAIL, L"Cannot derive plugin preference filename.");
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring name;
    for (auto byte : bytes) { name += digits[byte >> 4]; name += digits[byte & 15]; }
    return name + L".json";
}
JsonObject Read(std::filesystem::path const& path) {
    auto raw = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return JsonObject{};
        throw hresult_error(HRESULT_FROM_WIN32(error));
    }
    handle file{raw}; CheckFile(file.get(), false);
    LARGE_INTEGER length{}; check_bool(GetFileSizeEx(file.get(), &length));
    if (length.QuadPart <= 0 || length.QuadPart > MaxBytes) throw std::runtime_error("Invalid plugin preference file size.");
    std::string text(static_cast<std::size_t>(length.QuadPart), '\0');
    DWORD got{}; check_bool(ReadFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &got, nullptr));
    if (got != text.size()) throw std::runtime_error("Incomplete plugin preference read.");
    return Parse(text);
}
void Write(std::filesystem::path const& path, std::string const& bytes) {
    auto temp = path;
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(serial++);
    auto raw = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) throw_last_error();
    handle file{raw};
    try {
        CheckFile(file.get(), false);
        DWORD written{}; check_bool(WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
        if (written != bytes.size()) throw std::runtime_error("Incomplete plugin preference write.");
        check_bool(FlushFileBuffers(file.get()));
        file.close();
        check_bool(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    } catch (...) { file.close(); DeleteFileW(temp.c_str()); throw; }
}
}
std::string PluginPreferenceStore::Normalize(std::string_view json) {
    Apartment apartment;
    return to_string(Parse(json).Stringify());
}
std::string PluginPreferenceStore::Load(std::string_view identifier) {
    Apartment apartment;
    auto filename = Filename(identifier);
    Directory directory; auto lock = Lock(directory);
    return to_string(Read(directory.path / filename).Stringify());
}
std::string PluginPreferenceStore::SaveMerged(std::string_view identifier,
    std::string_view baseJson, std::string_view editedJson) {
    Apartment apartment;
    auto filename = Filename(identifier);
    auto base = Parse(baseJson), edited = Parse(editedJson);
    Directory directory; auto lock = Lock(directory);
    auto path = directory.path / filename;
    auto current = Read(path); // any new corruption rejects this save
    for (auto const& item : base) {
        if (!edited.HasKey(item.Key()) && current.HasKey(item.Key())) current.Remove(item.Key());
    }
    for (auto const& item : edited) {
        if (!base.HasKey(item.Key()) || !Equal(base.Lookup(item.Key()), item.Value())) current.Insert(item.Key(), item.Value());
    }
    // Merge can exceed the per-instance limit even when both inputs fit.
    auto result = to_string(current.Stringify());
    result = to_string(Parse(result).Stringify());
    Write(path, result);
    return result;
}
}
