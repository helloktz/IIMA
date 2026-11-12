// JavascriptPlugin.init / resolvePath and PrefPluginPermissionListView adaptation.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginInspection.h"
#include "../../IIMA.Core/Plugins/PluginManifest.h"
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Data::Json;
void CheckStop(std::stop_token stop) {
    if (stop.stop_requested()) throw hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
}
// Retain ancestor handles without delete sharing so an ordinary directory
// cannot be exchanged for a junction between checks. No directory enumeration.
struct LocalFolder {
    std::filesystem::path root;
    std::vector<handle> ancestors;
    explicit LocalFolder(std::filesystem::path const& input) {
        auto text = input.native();
        if (text.size() < 3 || !((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) ||
            text[1] != L':' || (text[2] != L'\\' && text[2] != L'/') ||
            text.find(L'\0') != text.npos || text.find(L':', 2) != text.npos)
            throw hresult_invalid_argument(L"仅支持绝对本地磁盘目录。");
        root = input.lexically_normal();
        auto drive = root.root_path().wstring();
        auto type = GetDriveTypeW(drive.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            throw hresult_invalid_argument(L"不检查网络插件目录。");
        auto prefix = root.root_path();
        for (auto const& part : root.relative_path()) {
            if (part.empty()) continue;
            prefix /= part;
            auto raw = CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (raw == INVALID_HANDLE_VALUE) throw_last_error();
            handle current(raw);
            FILE_ATTRIBUTE_TAG_INFO info{};
            check_bool(GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &info, sizeof(info)));
            if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)))
                throw hresult_error(E_ACCESSDENIED, L"不检查链接、重解析点或离线目录。");
            ancestors.push_back(std::move(current));
        }
    }
    handle Open(std::string const& relative) const {
        auto wide = to_hstring(relative);
        std::filesystem::path tail(wide.c_str());
        // Caller validates manifest paths; this also protects the fixed Info.json.
        if (tail.empty() || tail.is_absolute() || tail.has_root_name()) throw hresult_invalid_argument();
        auto prefix = root;
        std::vector<handle> held;
        for (auto const& part : tail.parent_path()) {
            if (part == L".." || part == L".") throw hresult_invalid_argument();
            prefix /= part;
            auto raw = CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (raw == INVALID_HANDLE_VALUE) throw_last_error();
            handle current(raw);
            FILE_ATTRIBUTE_TAG_INFO info{};
            check_bool(GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &info, sizeof(info)));
            if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)))
                throw hresult_error(E_ACCESSDENIED, L"插件路径包含链接或离线目录。");
            held.push_back(std::move(current));
        }
        auto target = root / tail;
        auto raw = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (raw == INVALID_HANDLE_VALUE) throw_last_error();
        handle file(raw);
        FILE_ATTRIBUTE_TAG_INFO info{};
        check_bool(GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &info, sizeof(info)));
        if (GetFileType(raw) != FILE_TYPE_DISK || (info.FileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)))
            throw hresult_error(E_ACCESSDENIED, L"插件入口必须是普通本地文件。");
        return file;
    }
};
std::string ReadManifest(LocalFolder const& folder, std::stop_token stop) {
    auto file = folder.Open("Info.json");
    LARGE_INTEGER size{};
    check_bool(GetFileSizeEx(file.get(), &size));
    if (size.QuadPart <= 0 || size.QuadPart > 1024 * 1024)
        throw hresult_invalid_argument(L"Info.json 必须为非空文件，且不超过 1 MiB。");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset{};
    while (offset < bytes.size()) {
        CheckStop(stop);
        DWORD read{};
        check_bool(ReadFile(file.get(), bytes.data() + offset,
            static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - offset)), &read, nullptr));
        if (!read) throw hresult_error(E_FAIL, L"Info.json 读取不完整。");
        offset += read;
    }
    // Bound nesting before invoking the native JSON parser. Strings and escapes
    // are skipped; malformed syntax is still rejected by JsonObject::Parse.
    bool quoted{}, escape{}; int depth{};
    for (unsigned char c : bytes) {
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 32) throw hresult_invalid_argument(L"插件清单嵌套过深。"); }
        else if (c == '}' || c == ']') --depth;
    }
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
    return bytes;
}
std::optional<std::string> String(JsonObject const& object, wchar_t const* key) {
    if (!object.HasKey(key)) return std::nullopt;
    auto value = object.GetNamedValue(key);
    if (value.ValueType() != JsonValueType::String) throw hresult_invalid_argument(L"插件清单字符串字段类型错误。");
    auto result = to_string(value.GetString());
    if (result.size() > 16384 || result.find('\0') != result.npos)
        throw hresult_invalid_argument(L"插件清单字符串过长或包含 NUL。");
    return result;
}
std::vector<std::string> Strings(JsonObject const& object, wchar_t const* key) {
    std::vector<std::string> result;
    if (!object.HasKey(key)) return result;
    auto array = object.GetNamedArray(key);
    if (array.Size() > 256) throw hresult_invalid_argument(L"插件清单数组过大。");
    for (auto const& value : array) {
        if (value.ValueType() != JsonValueType::String) throw hresult_invalid_argument(L"插件清单数组类型错误。");
        auto text = to_string(value.GetString());
        if (text.size() > 4096 || text.find('\0') != text.npos) throw hresult_invalid_argument();
        result.push_back(std::move(text));
    }
    return result;
}
}
std::wstring InspectPluginFolder(std::filesystem::path const& root, std::stop_token stop) {
    CheckStop(stop);
    LocalFolder folder(root);
    auto json = JsonObject::Parse(to_hstring(ReadManifest(folder, stop)));
    PluginManifestInput input;
    input.name = String(json, L"name"); input.identifier = String(json, L"identifier");
    input.version = String(json, L"version"); input.entry = String(json, L"entry");
    input.globalEntry = String(json, L"globalEntry"); input.description = String(json, L"description");
    input.preferencesPage = String(json, L"preferencesPage"); input.helpPage = String(json, L"helpPage");
    input.ghRepo = String(json, L"ghRepo");
    if (json.HasKey(L"ghVersion")) {
        auto value = json.GetNamedNumber(L"ghVersion");
        if (!std::isfinite(value) || value < 0 || value > 9007199254740991.0 || std::floor(value) != value)
            throw hresult_invalid_argument(L"ghVersion 必须为安全的非负整数。");
        input.ghVersion = static_cast<std::int64_t>(value);
    }
    if (json.HasKey(L"sidebarTab")) input.sidebarTabName = String(json.GetNamedObject(L"sidebarTab"), L"name");
    if (json.HasKey(L"subtitleProviders")) {
        auto providers = json.GetNamedArray(L"subtitleProviders");
        if (providers.Size() > 128) throw hresult_invalid_argument(L"字幕提供商声明过多。");
        for (auto const& value : providers) {
            auto provider = value.GetObject();
            input.subtitleProviders.push_back({String(provider, L"id").value_or(""), String(provider, L"name").value_or("")});
        }
    }
    if (json.HasKey(L"author")) {
        auto author = json.GetNamedObject(L"author");
        input.authorName = String(author, L"name"); input.authorEmail = String(author, L"email");
        input.authorUrl = String(author, L"url");
    }
    input.permissions = Strings(json, L"permissions"); input.allowedDomains = Strings(json, L"allowedDomains");
    // Compatibility report deliberately does not instantiate HTML pages,
    // subtitle providers or preference defaults as executable product features.
    auto result = ValidatePluginManifest(input);
    std::wostringstream report;
    report << L"只读插件检查，不安装、不启用、不运行脚本。\n\n";
    if (input.name) report << L"名称: " << to_hstring(*input.name).c_str() << L"\n";
    if (input.identifier) report << L"标识: " << to_hstring(*input.identifier).c_str() << L"\n";
    if (input.version) report << L"版本: " << to_hstring(*input.version).c_str() << L"\n";
    if (input.authorName) report << L"作者: " << to_hstring(*input.authorName).c_str() << L"\n";
    report << L"\n声明权限（未授予）:\n";
    for (auto const& permission : input.permissions) report << L" • " << to_hstring(permission).c_str() << L"\n";
    if (input.permissions.empty()) report << L" 无\n";
    report << L"\n允许域名（未授权访问）:\n";
    for (auto const& domain : input.allowedDomains) report << L" • " << to_hstring(domain).c_str() << L"\n";
    for (auto const& issue : result.issues) report << L"\n" << to_hstring(issue.field + ": " + issue.message).c_str();
    if (result.manifest) {
        auto check = [&](std::string const& path, bool required) {
            CheckStop(stop);
            try { auto file = folder.Open(path); report << L"\n已找到普通文件: " << to_hstring(path).c_str(); }
            catch (hresult_error const& e) { report << (required ? L"\n入口不可读取: " : L"\n可选文件不可读取: ") << to_hstring(path).c_str() << L" (" << e.message().c_str() << L")"; }
        };
        check(result.manifest->entry, true);
        if (result.manifest->globalEntry) check(*result.manifest->globalEntry, true);
        if (result.manifest->preferencesPage) check(*result.manifest->preferencesPage, false);
        report << L"\n\n清单字段通过静态校验；这不是可执行性或插件安全审计结论。";
    } else report << L"\n\n清单校验未通过。";
    report << L"\n当前没有 JavaScript 运行时、IINA 插件 API、HTML 页面或插件字幕提供商支持。";
    if (json.HasKey(L"localized")) report << L"\nlocalized 信息尚未投影，显示基础名称。";
    if (json.HasKey(L"preferenceDefaults")) report << L"\npreferenceDefaults 未应用或持久化。";
    if (json.HasKey(L"subtitleProviders")) report << L"\nsubtitleProviders 未注册。";
    return report.str();
}
}
