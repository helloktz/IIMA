#include "../src/IIMA.Core/Input/LocalInputPolicy.h"
#include <cassert>
#include <algorithm>
int main() {
    using namespace iima;
    assert(IsPlaylistPath(L"C:\\电影\\mix.M3U8"));
    assert(IsPlaylistPath(L"album.CUE"));
    assert(!AcceptExplicitFile(L"x.SRT"));
    assert(!AcceptExplicitFile(L"x.pls"));
    assert(AcceptExplicitFile(L"unknown.custom"));
    assert(AcceptExplicitFile(L"extensionless"));
    assert(AcceptExplicitFile(L"album.cue"));
    assert(AcceptDiscoveredFile(L"x.MP4"));
    assert(AcceptDiscoveredFile(L"x.FLAC"));
    assert(!AcceptDiscoveredFile(L"x.custom"));
    assert(!AcceptDiscoveredFile(L"x.m3u"));
    assert(!AcceptDiscoveredFile(L"x.srt"));
    assert(!AcceptDiscoveredFile(L"folder.mp4\\extensionless"));
    std::vector<std::wstring> paths={L"C:\\B\\e1.mp4",L"C:\\A\\e10.mp4",L"C:\\A\\e2.mp4",L"C:\\A\\e2.mp4",L"C:\\A\\e1.mp4"};
    SortUniquePaths(paths);
    assert((paths==std::vector<std::wstring>{L"C:\\A\\e1.mp4",L"C:\\A\\e2.mp4",L"C:\\A\\e10.mp4",L"C:\\B\\e1.mp4"}));
    std::vector<std::wstring> huge={L"x100000000000000000000000.mp4",L"x9.mp4",L"x0009.mp4",L"X9.mp4",L"电影2.mp4",L"电影10.mp4"};
    auto original=huge;
    SortUniquePaths(huge);
    assert(std::find(huge.begin(),huge.end(),L"电影2.mp4")<std::find(huge.begin(),huge.end(),L"电影10.mp4"));
    auto sorted=huge;
    std::reverse(huge.begin(),huge.end()); SortUniquePaths(huge); assert(huge==sorted);
    SortUniquePaths(original); assert(original==sorted);
    std::vector<std::wstring> empty; SortUniquePaths(empty); assert(empty.empty());
}
