#pragma once
// Bounded versioned activation wire format. Uses explicit UTF-16 code units so
// parser/validation tests run independently of Windows and host wchar_t width.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
namespace iima::activation {
inline constexpr std::uint32_t Magic = 0x414D4949;
inline constexpr std::uint32_t Version = 1;
inline constexpr std::uint32_t MaxPaths = 256;
inline constexpr std::uint32_t MaxPayload = 1024 * 1024;
inline void Append(std::vector<std::byte>& bytes, std::uint32_t value) {
    auto offset = bytes.size(); bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
inline bool Take(std::vector<std::byte> const& bytes, std::size_t& offset, std::uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(value)); offset += sizeof(value); return true;
}
inline bool ValidPath(std::u16string const& path) {
    // Absolute drive or UNC path. Reject device namespaces, embedded NULs,
    // quotes, and alternate streams. Never reinterpret an input as an option.
    if (path.empty() || path.size() > 32767 || path.find(u'\0') != std::u16string::npos || path.find(u'"') != std::u16string::npos) return false;
    if (path.starts_with(u"\\\\?\\") || path.starts_with(u"\\\\.\\")) return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        auto code = static_cast<std::uint16_t>(path[i]);
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (++i == path.size()) return false;
            auto low = static_cast<std::uint16_t>(path[i]);
            if (low < 0xDC00 || low > 0xDFFF) return false;
        } else if (code >= 0xDC00 && code <= 0xDFFF) return false;
    }
    bool drive = path.size() >= 3 && ((path[0] >= u'A' && path[0] <= u'Z') || (path[0] >= u'a' && path[0] <= u'z')) && path[1] == u':' && (path[2] == u'\\' || path[2] == u'/');
    bool unc = path.size() > 2 && path[0] == u'\\' && path[1] == u'\\';
    return (drive || unc) && path.find(u':', drive ? 2 : 0) == std::u16string::npos;
}
inline std::optional<std::vector<std::byte>> Encode(std::vector<std::u16string> const& inputs) {
    static_assert(sizeof(char16_t) == 2);
    if (inputs.size() > MaxPaths) return std::nullopt;
    std::vector<std::byte> bytes;
    Append(bytes, Magic); Append(bytes, Version); Append(bytes, static_cast<std::uint32_t>(inputs.size()));
    for (auto const& path : inputs) {
        if (!ValidPath(path)) return std::nullopt;
        auto size = path.size() * sizeof(char16_t);
        if (bytes.size() + sizeof(std::uint32_t) + size > MaxPayload) return std::nullopt;
        Append(bytes, static_cast<std::uint32_t>(path.size()));
        auto offset = bytes.size(); bytes.resize(offset + size); std::memcpy(bytes.data() + offset, path.data(), size);
    }
    return bytes;
}
inline std::optional<std::vector<std::u16string>> Decode(std::vector<std::byte> const& bytes) {
    if (bytes.size() > MaxPayload) return std::nullopt;
    std::vector<std::u16string> result;
    std::size_t offset{}; std::uint32_t magic{}, version{}, count{};
    if (!Take(bytes, offset, magic) || !Take(bytes, offset, version) || !Take(bytes, offset, count) || magic != Magic || version != Version || count > MaxPaths) return std::nullopt;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t chars{};
        if (!Take(bytes, offset, chars) || !chars || chars > 32767 || chars > (bytes.size() - offset) / sizeof(char16_t)) return std::nullopt;
        std::u16string path(chars, u'\0'); std::memcpy(path.data(), bytes.data() + offset, chars * sizeof(char16_t)); offset += chars * sizeof(char16_t);
        if (!ValidPath(path)) return std::nullopt;
        result.push_back(std::move(path));
    }
    if (offset != bytes.size()) return std::nullopt;
    return result;
}
}
