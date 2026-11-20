#pragma once

namespace iima::loc {
void Initialize();
wchar_t const* Text(wchar_t const* id) noexcept;
char const* Utf8(wchar_t const* id) noexcept;
}
