// IINA FontPickerWindowController family filtering/localized listing adapted
// to DirectWrite. Copyright (c) 2016 lhc and contributors. GPL-3.0.
#include "pch.h"
#include "FontCatalog.h"
#include <dwrite.h>
#include <algorithm>
#include <cwchar>
#include <optional>
#include <set>
#include <string_view>

#pragma comment(lib, "dwrite.lib")
namespace iima {
namespace {
constexpr UINT32 MaxFamilies = 8192;
constexpr UINT32 MaxNameLength = 512;
void CheckCancellation(std::stop_token stop) {
    if (stop.stop_requested()) throw winrt::hresult_canceled();
}
std::optional<std::wstring> NameAt(IDWriteLocalizedStrings* names, UINT32 index) {
    UINT32 length{};
    if (FAILED(names->GetStringLength(index, &length)) || !length || length > MaxNameLength)
        return {};
    std::wstring value(length + 1, L'\0');
    if (FAILED(names->GetString(index, value.data(), length + 1))) return {};
    value.resize(length);
    // Embedded NUL and invalid UTF-16 must not become truncated mpv input.
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = static_cast<unsigned>(value[i]);
        if (c < 0x20 || c == 0x7f) return {};
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == value.size()) return {};
            auto low = static_cast<unsigned>(value[i]);
            if (low < 0xdc00 || low > 0xdfff) return {};
        } else if (c >= 0xdc00 && c <= 0xdfff) return {};
    }
    if (value.front() == L'.' || value.find_first_not_of(L" \t\r\n\u00a0\u3000") == std::wstring::npos)
        return {};
    return value;
}
std::optional<UINT32> LocaleIndex(IDWriteLocalizedStrings* names, wchar_t const* locale) {
    UINT32 index{}; BOOL exists{};
    if (SUCCEEDED(names->FindLocaleName(locale, &index, &exists)) && exists) return index;
    return {};
}
int Compare(std::wstring const& a, std::wstring const& b, bool ignoreCase) {
    const int result = CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), ignoreCase);
    if (result) return result - CSTR_EQUAL;
    return a.compare(b); // deterministic fallback; inputs are bounded UTF-16
}
struct FamilyLess {
    bool operator()(std::wstring const& a, std::wstring const& b) const {
        return Compare(a, b, true) < 0;
    }
};
}
std::vector<FontFamilyEntry> FontCatalog::Enumerate(std::stop_token stop) {
    CheckCancellation(stop);
    winrt::com_ptr<IDWriteFactory> factory;
    winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.put())));
    winrt::com_ptr<IDWriteFontCollection> collection;
    // Original DirectWrite collection includes installed fonts only. Unlike
    // newer remote-font APIs this does not enqueue cloud font downloads.
    winrt::check_hresult(factory->GetSystemFontCollection(collection.put(), FALSE));
    CheckCancellation(stop);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
        wcscpy_s(locale, L"en-us");
    const auto total = collection->GetFontFamilyCount();
    if (total > MaxFamilies) throw winrt::hresult_error(E_BOUNDS, L"Installed font family limit exceeded.");
    const auto count = total;
    std::vector<FontFamilyEntry> result;
    result.reserve(count);
    std::set<std::wstring, FamilyLess> seen;
    for (UINT32 i = 0; i < count; ++i) {
        CheckCancellation(stop);
        winrt::com_ptr<IDWriteFontFamily> family;
        winrt::com_ptr<IDWriteLocalizedStrings> names;
        if (FAILED(collection->GetFontFamily(i, family.put())) ||
            FAILED(family->GetFamilyNames(names.put())) || !names->GetCount()) {
            continue;
        }
        auto english = LocaleIndex(names.get(), L"en-us");
        auto name = NameAt(names.get(), english.value_or(0));
        if (!name && english) name = NameAt(names.get(), 0);
        if (!name || !seen.insert(*name).second) { continue; }
        auto local = LocaleIndex(names.get(), locale);
        auto display = local ? NameAt(names.get(), *local) : std::optional<std::wstring>{};
        auto displayName = display.value_or(*name);
        result.push_back({std::move(*name), std::move(displayName)});
    }
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
        const auto order = Compare(a.displayName, b.displayName, true);
        return order ? order < 0 : Compare(a.name, b.name, false) < 0;
    });
    CheckCancellation(stop);
    return result;
}
}
