// JavascriptPlugin.init / resolvePath and PrefPluginPermissionListView adaptation.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginInspection.h"
#include "PluginPreferenceStore.h"
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

std::string ReadProgram(LocalFolder const& folder, std::string const& path, std::stop_token stop, bool allowEmpty = false) {
    auto file = folder.Open(path);
    LARGE_INTEGER size{};
    check_bool(GetFileSizeEx(file.get(), &size));
    if (size.QuadPart < 0 || (!allowEmpty && size.QuadPart == 0) || size.QuadPart > 1024 * 1024)
        throw hresult_invalid_argument(L"插件源文件须为 1 MiB 以内的 UTF-8 文本，入口不能为空。");
    std::string source(static_cast<std::size_t>(size.QuadPart), '\0');
    for (std::size_t offset{}; offset < source.size();) {
        CheckStop(stop);
        DWORD read{};
        check_bool(ReadFile(file.get(), source.data() + offset,
            static_cast<DWORD>(std::min<std::size_t>(65536, source.size() - offset)), &read, nullptr));
        if (!read) throw hresult_error(E_FAIL, L"插件入口读取不完整。");
        offset += read;
    }
    if (source.starts_with("\xef\xbb\xbf")) source.erase(0, 3);
    if (source.empty() && allowEmpty) return source;
    if (source.find('\0') != source.npos || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        source.data(), static_cast<int>(source.size()), nullptr, 0))
        throw hresult_invalid_argument(L"插件入口不是有效 UTF-8 文本。");
    return source;
}

// Snapshot only ordinary .js package files before showing execution consent.
// The runtime receives bytes, never a filesystem handle or arbitrary path API.
std::map<std::string, std::string> ReadModules(LocalFolder const& package,
    std::string const& entry, std::string const& source, std::stop_token stop) {
    std::map<std::string, std::string> result;
    std::size_t bytes = source.size(), directories{};
    auto walk = [&](auto&& self, std::filesystem::path const& relative, unsigned depth) -> void {
        CheckStop(stop);
        if (depth > 16 || ++directories > 128)
            throw hresult_invalid_argument(L"插件目录超过 16 层或 128 个目录，不能建立完整源快照。");
        // Holds all directory ancestors against replacement for this enumeration.
        LocalFolder held(package.root / relative);
        WIN32_FIND_DATAW data{};
        auto raw = FindFirstFileExW((held.root / L"*").c_str(), FindExInfoBasic, &data,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (raw == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return;
            throw_last_error();
        }
        struct FindHandle { HANDLE handle; ~FindHandle() { FindClose(handle); } } finder{raw};
        for (;;) {
            CheckStop(stop);
            std::wstring name(data.cFileName);
            if (name != L"." && name != L"..") {
                auto child = relative / name;
                if (data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) {
                    // No require can read a skipped target from the immutable map.
                } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // Package metadata is not part of the executable source snapshot.
                    if (name != L".git") self(self, child, depth + 1);
                } else if (CompareStringOrdinal(child.extension().c_str(), -1, L".js", -1, TRUE) == CSTR_EQUAL) {
                    auto key = to_string(child.generic_wstring());
                    auto normalized = NormalizePluginRelativePath(key);
                    if (!normalized) throw hresult_invalid_argument(L"插件模块包含不安全的文件名。");
                    if (*normalized != entry) {
                        if (result.size() >= 128) throw hresult_invalid_argument(L"插件最多支持 128 个本地 JS 模块。");
                        auto text = ReadProgram(package, *normalized, stop, true);
                        if (text.size() > 8 * 1024 * 1024 - bytes)
                            throw hresult_invalid_argument(L"插件源快照总量超过 8 MiB。");
                        bytes += text.size();
                        result.emplace(std::move(*normalized), std::move(text));
                    }
                }
            }
            if (!FindNextFileW(raw, &data)) {
                if (GetLastError() != ERROR_NO_MORE_FILES) throw_last_error();
                break;
            }
        }
    };
    walk(walk, {}, 0);
    return result;
}

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
PluginManifestInput ParseManifest(JsonObject const& json) {
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
    return input;
}

}
std::wstring InspectPluginFolder(std::filesystem::path const& root, std::stop_token stop) {
    CheckStop(stop);
    LocalFolder folder(root);
    auto json = JsonObject::Parse(to_hstring(ReadManifest(folder, stop)));
    auto input = ParseManifest(json);
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
    report << L"\n另有手动启动的受限插件运行入口；HTML 页面、插件字幕提供商和大部分 IINA API 仍不支持。";
    if (json.HasKey(L"localized")) report << L"\nlocalized 信息尚未投影，显示基础名称。";
    if (json.HasKey(L"preferenceDefaults")) report << L"\npreferenceDefaults 仅在明确运行后由受限 preferences API 应用；此检查不读取已保存偏好。";
    if (json.HasKey(L"subtitleProviders")) report << L"\nsubtitleProviders 未注册。";
    return report.str();
}
PluginProgram LoadPluginProgram(std::filesystem::path const& root, std::stop_token stop) {
    CheckStop(stop);
    LocalFolder folder(root);
    auto json = JsonObject::Parse(to_hstring(ReadManifest(folder, stop)));
    auto validation = ValidatePluginManifest(ParseManifest(json));
    if (!validation.manifest) throw hresult_invalid_argument(L"插件清单未通过检查，不能执行。");
    auto manifest = std::move(*validation.manifest);
    if (manifest.globalEntry) throw hresult_invalid_argument(L"本阶段不运行声明全局入口的插件。");
    if (!manifest.unknownPermissions.empty())
        throw hresult_invalid_argument(L"插件声明了不支持的权限，不能执行。");
    for (auto permission : manifest.permissions) {
        if (permission != PluginPermission::ShowOSD && permission != PluginPermission::NetworkRequest)
            throw hresult_invalid_argument(L"本阶段插件执行仅支持 show-osd 和受限 network-request 权限；任意文件、HTML 和弹窗权限不可用。");
    }
    auto source = ReadProgram(folder, manifest.entry, stop);
    CheckStop(stop);
    auto modules = ReadModules(folder, manifest.entry, source, stop);
    std::string defaults = "{}";
    if (json.HasKey(L"preferenceDefaults")) {
        auto value = json.GetNamedValue(L"preferenceDefaults");
        if (value.ValueType() != JsonValueType::Object)
            throw hresult_invalid_argument(L"preferenceDefaults 必须是 JSON 对象。");
        defaults = PluginPreferenceStore::Normalize(to_string(value.Stringify()));
    }
    return {std::move(manifest), std::move(source), std::move(defaults), std::move(modules)};
}

}
