#include "../src/IIMA.App/Platform/InstanceProtocol.h"
#include <cassert>
#include <array>
#include <iostream>
using namespace iima::activation;
int main() {
    std::vector<std::u16string> paths{u"C:\\媒体\\视频 1.mkv", u"D:/movie.mp4", u"\\\\server\\share\\clip.mp4", u"C:\\emoji-\U0001F3AC.mkv"};
    auto bytes = Encode(paths); assert(bytes);
    auto result = Decode(*bytes); assert(result && *result == paths);
    auto empty = Encode({}); assert(empty && Decode(*empty)->empty());
    // All truncations, trailing data, wrong magic/version/count are rejected.
    for (std::size_t i = 0; i < bytes->size(); ++i) {
        std::vector<std::byte> partial(bytes->begin(), bytes->begin() + i);
        assert(!Decode(partial));
    }
    auto trailing = *bytes; trailing.push_back(std::byte{0}); assert(!Decode(trailing));
    for (auto offset : {0, 4, 8, 12}) {
        auto malformed = *bytes;
        for (int i = 0; i < 4; ++i) malformed[offset + i] = std::byte{0xff};
        assert(!Decode(malformed));
    }
    std::vector<std::u16string> bad{u"", u"relative.mkv", u"https://example.com/a", u"C:relative", u"C:\\file:stream", u"\\\\?\\C:\\x", u"\\\\.\\pipe\\x", u"C:\\a\".mp4"};
    bad.emplace_back(u"C:\\x\0y", 7);
    bad.push_back(std::u16string{u'C',u':',u'\\',char16_t{0xD800}});
    bad.push_back(std::u16string{u'C',u':',u'\\',char16_t{0xDC00}});
    bad.push_back(std::u16string{u'C',u':',u'\\',char16_t{0xD800},u'x'});
    for (auto const& path : bad) assert(!Encode({path}));
    std::vector<std::u16string> maximum(MaxPaths, u"C:\\x"); assert(Encode(maximum));
    maximum.push_back(u"C:\\x"); assert(!Encode(maximum));
    auto longPath = u"C:\\" + std::u16string(32764, u'x'); assert(Encode({longPath}));
    assert(!Encode({longPath + u'x'}));
    std::vector<std::u16string> oversize(17, longPath); assert(!Encode(oversize));
    assert(!Decode(std::vector<std::byte>(MaxPayload + 1)));
    // Corrupt UTF-16 inside an otherwise well-framed input, not only encoder input.
    auto corrupted = *Encode({u"C:\\x"});
    const char16_t surrogate = 0xD800;
    std::memcpy(corrupted.data() + 16 + 3 * sizeof(char16_t), &surrogate, sizeof(surrogate));
    assert(!Decode(corrupted));
    std::cout << "PASS: bounded activation protocol / Unicode / invalid frame cases\n";
}
