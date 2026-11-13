#include "pch.h"
#include "FileAssociations.h"
#include <ShlObj.h>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace iima {
namespace {
constexpr wchar_t progId[] = L"IIMA.Media";
constexpr wchar_t capabilities[] = L"Software\\IIMA\\Capabilities";
constexpr wchar_t application[] = L"Software\\Classes\\Applications\\IIMA.exe";
constexpr wchar_t extensions[][8] = {L".mp4",L".mkv",L".mov",L".webm",L".avi",L".m4v",L".wmv",
    L".mp3",L".flac",L".wav",L".ogg",L".opus",L".m4a",L".aac",L".m3u",L".m3u8",L".pls",L".cue"};
void Check(LSTATUS code) { if (code != ERROR_SUCCESS) throw winrt::hresult_error(HRESULT_FROM_WIN32(code)); }
struct Key {
    HKEY value{};
    ~Key() { if(value) RegCloseKey(value); }
};
std::wstring Exe()
{
    std::wstring path(32768, L'\0');
    auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) winrt::throw_last_error();
    path.resize(count);
    // Win32 forbids literal quotes in a filename. Refuse instead of attempting
    // to escape an unexpected executable name in the shell command template.
    if (path.find_first_of(L"\"%") != std::wstring::npos)
        throw winrt::hresult_invalid_argument(L"播放器目录含有无法安全写入关联命令模板的字符，请移动到不含引号或百分号的目录。");
    return path;
}
std::wstring Command() { return L"\"" + Exe() + L"\" \"%1\""; }
void SetString(std::wstring const& path, wchar_t const* name, std::wstring const& value)
{
    Key key;
    Check(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&key.value,nullptr));
    Check(RegSetValueExW(key.value,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),
                        static_cast<DWORD>((value.size()+1)*sizeof(wchar_t))));
}
std::wstring GetString(std::wstring const& path, wchar_t const* name)
{
    DWORD bytes{};
    auto status = RegGetValueW(HKEY_CURRENT_USER,path.c_str(),name,RRF_RT_REG_SZ,nullptr,nullptr,&bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return {};
    Check(status);
    if (bytes > 128*1024 || bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0) throw winrt::hresult_invalid_argument();
    std::wstring value(bytes/sizeof(wchar_t),L'\0');
    Check(RegGetValueW(HKEY_CURRENT_USER,path.c_str(),name,RRF_RT_REG_SZ,nullptr,value.data(),&bytes));
    while(!value.empty() && value.back()==L'\0') value.pop_back();
    return value;
}
void DeleteTree(std::wstring const& path)
{
    auto status=RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());
    if(status!=ERROR_FILE_NOT_FOUND && status!=ERROR_PATH_NOT_FOUND) Check(status);
}
void DeleteValue(std::wstring const& path, wchar_t const* name)
{
    Key key;
    auto status=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_SET_VALUE,&key.value);
    if(status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND) return;
    Check(status);
    status=RegDeleteValueW(key.value,name);
    if(status!=ERROR_FILE_NOT_FOUND) Check(status);
}
}
void FileAssociations::Register()
{
    // This is idempotent. A failure is surfaced; retry completes a partial write.
    auto command=Command();
    SetString(capabilities,L"OwnerExecutable",Exe());
    SetString(L"Software\\Classes\\IIMA.Media",nullptr,L"IIMA 媒体文件");
    SetString(L"Software\\Classes\\IIMA.Media\\DefaultIcon",nullptr,L"\""+Exe()+L"\",0");
    SetString(L"Software\\Classes\\IIMA.Media\\shell\\open\\command",nullptr,command);
    SetString(application,L"FriendlyAppName",L"IIMA");
    SetString(std::wstring(application)+L"\\shell\\open\\command",nullptr,command);
    SetString(capabilities,L"ApplicationName",L"IIMA");
    SetString(capabilities,L"ApplicationDescription",L"IIMA 原生媒体播放器");
    for(auto const& extension:extensions) {
        SetString(std::wstring(capabilities)+L"\\FileAssociations",extension,progId);
        SetString(std::wstring(application)+L"\\SupportedTypes",extension,L"");
        Key key;
        auto path=std::wstring(L"Software\\Classes\\")+extension+L"\\OpenWithProgids";
        Check(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&key.value,nullptr));
        Check(RegSetValueExW(key.value,progId,0,REG_NONE,nullptr,0));
    }
    SetString(L"Software\\RegisteredApplications",L"IIMA",capabilities);
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
}
void FileAssociations::Unregister()
{
    auto current=GetString(L"Software\\Classes\\IIMA.Media\\shell\\open\\command",nullptr);
    auto appCommand=GetString(std::wstring(application)+L"\\shell\\open\\command",nullptr);
    auto expected=Command();
    auto owner=GetString(capabilities,L"OwnerExecutable");
    if (owner != Exe()) {
        if (owner.empty() && current.empty() && appCommand.empty() && GetString(capabilities,L"ApplicationName").empty()) return;
        throw winrt::hresult_error(E_ACCESSDENIED,L"无法确认此副本拥有该注册，未移除任何关联。");
    }
    if ((!current.empty() && current!=expected) || (!appCommand.empty() && appCommand!=expected))
        throw winrt::hresult_error(E_ACCESSDENIED,L"关联由另一位置的 IIMA 注册，请在该副本中取消注册。");
    for(auto const& extension:extensions)
        DeleteValue(std::wstring(L"Software\\Classes\\")+extension+L"\\OpenWithProgids",progId);
    DeleteTree(L"Software\\Classes\\IIMA.Media");
    DeleteTree(application);
    if(GetString(L"Software\\RegisteredApplications",L"IIMA")==capabilities)
        DeleteValue(L"Software\\RegisteredApplications",L"IIMA");
    DeleteTree(capabilities);
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
}
void FileAssociations::OpenDefaultApps(HWND owner)
{
    auto result=ShellExecuteW(owner,L"open",L"ms-settings:defaultapps",nullptr,nullptr,SW_SHOWNORMAL);
    if(reinterpret_cast<INT_PTR>(result)<=32) throw winrt::hresult_error(E_FAIL,L"无法打开 Windows 默认应用设置。");
}
}
