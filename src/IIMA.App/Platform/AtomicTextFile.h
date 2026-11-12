#pragma once
#include <filesystem>
#include <string_view>
namespace iima {
// Blocking UTF-8 write. Invoke off the UI thread. Destination is replaced only
// after the complete temporary file in the same directory has been flushed.
void WriteUtf8Atomically(std::filesystem::path const& destination, std::string_view text);
}
