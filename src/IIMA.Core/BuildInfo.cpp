#include "BuildInfo.h"

// Fail instead of silently compiling the scaffold in an older language mode.
static_assert(_MSVC_LANG > 202002L, "IIMA requires the C++23 language mode.");

std::string_view iima::version() noexcept
{
    return IIMA_VERSION;
}
