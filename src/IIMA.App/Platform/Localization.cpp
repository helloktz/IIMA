#include "pch.h"
#include "Localization.h"
#include <filesystem>
#include <map>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>
#include <winrt/Microsoft.Windows.Globalization.h>

namespace iima::loc {
namespace {
struct Message {
    winrt::hstring wide;
    std::string utf8;
};
std::map<std::wstring, Message, std::less<>> messages;

bool RunningFromInstalledFolder()
{
    wchar_t folder[32768]{};
    DWORD bytes = sizeof(folder);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\IIMA", L"InstallFolder",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, folder, &bytes) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) return false;
    auto installed = std::filesystem::path(folder).lexically_normal();
    if (!installed.has_filename()) installed = installed.parent_path();
    const auto running = std::filesystem::path(executable).parent_path();
    return CompareStringOrdinal(installed.c_str(), -1, running.c_str(), -1, TRUE) == CSTR_EQUAL;
}

wchar_t const* Language()
{
    if (RunningFromInstalledFolder()) {
        wchar_t selection[16]{};
        DWORD bytes = sizeof(selection);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\IIMA", L"Language",
                         RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, selection, &bytes) == ERROR_SUCCESS) {
            if (std::wstring_view(selection) == L"en-US") return L"en-US";
            if (std::wstring_view(selection) == L"zh-CN") return L"zh-CN";
        }
    }
    const auto systemLanguage = GetUserDefaultUILanguage();
    return systemLanguage == 0x0804 || systemLanguage == 0x1004 ? L"zh-CN" : L"en-US";
}
}

void Initialize()
{
    const auto language = Language();
    winrt::Microsoft::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(language);
    SetThreadUILanguage(LANGIDFROMLCID(LocaleNameToLCID(language, 0)));
    winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager resources;
    const auto context = resources.CreateResourceContext();
    context.QualifierValues().Insert(L"Language", language);
    const auto catalog = resources.MainResourceMap().GetSubtree(L"Messages");
    for (uint32_t index = 0; index < catalog.ResourceCount(); ++index) {
        const auto item = catalog.GetValueByIndex(index, context);
        const auto text = item.Value().ValueAsString();
        messages.emplace(item.Key().c_str(), Message{text, winrt::to_string(text)});
    }
}

wchar_t const* Text(wchar_t const* id) noexcept
{
    const auto item = messages.find(id);
    return item == messages.end() ? id : item->second.wide.c_str();
}

char const* Utf8(wchar_t const* id) noexcept
{
    const auto item = messages.find(id);
    return item == messages.end() ? "" : item->second.utf8.c_str();
}
}
