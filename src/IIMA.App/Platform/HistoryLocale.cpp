#include "pch.h"
#include "HistoryLocale.h"
#include <array>
#include <cstdio>
#include <climits>
#include <limits>
namespace iima {
namespace {
std::optional<HistoryLocalDate> LocalDate(std::int64_t timestamp) {
    // FILETIME is 100ns since 1601. Limit conversion to supported SYSTEMTIME
    // calendar range; malformed persisted future dates remain explicitly unknown.
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    if (timestamp < 0 || timestamp > 253402300799LL) return {};
    ULARGE_INTEGER ticks{}; ticks.QuadPart = epoch + static_cast<std::uint64_t>(timestamp) * 10000000ULL;
    FILETIME fileTime{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) return {};
    wchar_t label[256]{};
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &local, nullptr, label, 256, nullptr)) return {};
    char key[16]{};
    std::snprintf(key, sizeof(key), "%04u-%02u-%02u", static_cast<unsigned>(local.wYear),
        static_cast<unsigned>(local.wMonth), static_cast<unsigned>(local.wDay));
    return HistoryLocalDate{key, winrt::to_string(label)};
}
}
HistoryPresentation WindowsHistoryPresentation() {
    HistoryPresentation presentation;
    presentation.localDate = LocalDate;
    presentation.unknownDateLabel = "未知日期";
    presentation.unknownLocationLabel = "未知位置";
    presentation.localizedContains = [](std::string_view text, std::string_view query) {
        if (query.empty()) return true;
        auto haystack = winrt::to_hstring(text), needle = winrt::to_hstring(query);
        if (haystack.size() > INT_MAX || needle.size() > INT_MAX) return false;
        return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE | NORM_IGNORENONSPACE,
            haystack.c_str(), static_cast<int>(haystack.size()), needle.c_str(), static_cast<int>(needle.size()),
            nullptr, nullptr, nullptr, 0) >= 0;
    };
    presentation.localizedLess = [](std::string_view left, std::string_view right) {
        auto a = winrt::to_hstring(left), b = winrt::to_hstring(right);
        if (a.size() > INT_MAX || b.size() > INT_MAX) return left < right;
        auto order = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
            a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), nullptr, nullptr, 0);
        return order == 0 ? left < right : order == CSTR_LESS_THAN;
    };
    return presentation;
}
}
