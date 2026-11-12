// Local subtitle matching adapted from IINA AutoFileMatcher / Utility tables.
// Copyright © 2017 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Matching/SubtitleMatcher.h"
#include <algorithm>
#include <filesystem>
#include <thread>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::FindLocalSubtitle() {
    auto const& snapshot = session_->Snapshot();
    if (closing_ || snapshot.media != iima::MediaState::Loaded || snapshot.path.empty()) return;
    std::filesystem::path media(to_hstring(snapshot.path).c_str());
    // This explicit local action never interprets a URL as a filesystem path.
    if (!media.is_absolute()) { ShowError(L"此功能仅支持本地媒体路径。"); return; }
    subtitleMatchStop_.request_stop();
    subtitleMatchStop_ = std::stop_source{};
    auto token = subtitleMatchStop_.get_token();
    auto generation = ++subtitleMatchGeneration_;
    auto entryId = snapshot.entryId;
    auto weak = get_weak();
    auto dispatcher = DispatcherQueue();
    StatusText().Text(L"正在匹配同目录字幕…");
    std::thread([media = std::move(media), token, generation, entryId, weak, dispatcher]() mutable {
        std::vector<std::filesystem::path> subtitles;
        std::wstring errorText;
        iima::SubtitleMatchResult result;
        try {
            std::error_code error;
            std::size_t visited{};
            std::filesystem::directory_iterator iterator(media.parent_path(), error), end;
            for (; !error && iterator != end && !token.stop_requested(); iterator.increment(error)) {
                if (++visited > 10000) { errorText = L"目录过大，请手动选择字幕。"; break; }
                if (!iterator->is_regular_file(error)) continue;
                auto extension = iterator->path().extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) {
                    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + L'a' - L'A') : c;
                });
                static constexpr std::wstring_view accepted[]{L".utf", L".utf8", L".utf-8", L".idx", L".sub", L".srt",
                    L".smi", L".rt", L".ssa", L".aqt", L".jss", L".js", L".ass", L".mks", L".vtt", L".sup", L".scc", L".lrc"};
                if (std::find(std::begin(accepted), std::end(accepted), extension) != std::end(accepted))
                    subtitles.push_back(iterator->path());
            }
            if (error) errorText = L"无法读取媒体所在目录。";
            if (errorText.empty() && !token.stop_requested()) {
                std::vector<std::filesystem::path> mediaFiles{media};
                result = iima::MatchSubtitles(mediaFiles, subtitles, {}, generation,
                    [token] { return token.stop_requested(); });
            }
        } catch (...) { errorText = L"匹配字幕失败。"; }
        if (token.stop_requested()) return;
        try {
            dispatcher.TryEnqueue([weak, token, generation, entryId, media = std::move(media),
                result = std::move(result), errorText = std::move(errorText)]() {
                auto self = weak.get();
                if (!self || self->closing_ || token.stop_requested() ||
                    self->subtitleMatchGeneration_ != generation || result.cancelled) return;
                auto const& current = self->session_->Snapshot();
                // Entry ID plus path rejects stale results across replacement or
                // repeated opening of a different item, even if its name matches.
                if (current.entryId != entryId || current.media != iima::MediaState::Loaded ||
                    std::filesystem::path(to_hstring(current.path).c_str()) != media) return;
                if (!errorText.empty()) { self->ShowError(hstring(errorText)); return; }
                if (result.media.empty() || result.media.front().subtitles.empty()) {
                    self->StatusText().Text(L"未找到匹配字幕。"); return;
                }
                // The explicit action selects only the highest-ranked candidate;
                // avoid repeatedly overriding selection while adding all matches.
                self->session_->AddSubtitle(to_string(result.media.front().subtitles.front().native()));
            });
        } catch (...) { /* Dispatcher is closing; discard owned results. */ }
    }).detach();
}
}
