// IINA JavascriptAPIPlaylist / PlayerCore playlist operations, Windows adapter.
// Copyright (c) 2020 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace winrt::IIMA::implementation {
void MainWindow::DispatchPluginPlaylistCommand(iima::plugins::PluginCommand const& command) {
    if (closing_ || !pluginRuntime_) return;
    auto reject = [this](wchar_t const* message) {
        // Command acceptance was asynchronous, never a promise of mpv success.
        // Surface stale/blocked operations rather than silently claiming success.
        PluginRuntimeStatus().Text(message);
    };
    auto const& state = session_->Snapshot();
    if (command.mediaToken != std::to_string(pluginMediaEpoch_) ||
        state.session != iima::SessionState::Ready ||
        (state.media != iima::MediaState::Loading && state.media != iima::MediaState::Loaded)) {
        reject(L"插件播放列表操作已取消：媒体已改变或当前未播放。");
        return;
    }
    if (session_->PlaylistEditBusy()) {
        reject(L"插件播放列表操作已拒绝：另一个列表编辑尚未完成。");
        return;
    }
    try {
        using namespace Windows::Data::Json;
        if (command.text.size() > 64 * 1024 || state.playlist.size() > 1024)
            throw std::invalid_argument("Playlist action exceeds its budget");
        auto payload = JsonObject::Parse(to_hstring(command.text));
        auto ids = payload.GetNamedArray(L"ids");
        if (ids.Size() != state.playlist.size()) {
            reject(L"插件播放列表操作已取消：播放列表已改变。"); return;
        }
        for (std::uint32_t index = 0; index < ids.Size(); ++index) {
            if (ids.GetStringAt(index) != to_hstring(std::to_string(state.playlist[index].id))) {
                reject(L"插件播放列表操作已取消：播放列表顺序已改变。"); return;
            }
        }
        auto readIndex = [&state](IJsonValue const& value) -> std::size_t {
            if (value.ValueType() != JsonValueType::Number)
                throw std::invalid_argument("Expected playlist index");
            const auto number = value.GetNumber();
            if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
                number >= static_cast<double>(state.playlist.size()))
                throw std::invalid_argument("Invalid playlist index");
            return static_cast<std::size_t>(number);
        };
        if (command.name == "playlist.next") session_->PlaylistNext();
        else if (command.name == "playlist.previous") session_->PlaylistPrevious();
        else if (command.name == "playlist.move") {
            auto from = readIndex(payload.GetNamedValue(L"from"));
            auto to = readIndex(payload.GetNamedValue(L"to"));
            if (from != to) {
                // IINA forwards mpv playlist-move: destination identifies an
                // entry BEFORE removal, not the moved item's final index.
                session_->DropPlaylistItems({from}, to);
            }
        } else if (command.name == "playlist.play" || command.name == "playlist.remove") {
            auto values = payload.GetNamedArray(L"indices");
            if (values.Size() > state.playlist.size() ||
                (command.name == "playlist.play" && values.Size() != 1))
                throw std::invalid_argument("Invalid playlist action size");
            std::vector<std::size_t> indices;
            for (auto const& value : values) indices.push_back(readIndex(value));
            std::sort(indices.begin(), indices.end());
            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
            if (command.name == "playlist.play") session_->PlaylistPlay(indices.front());
            else if (!indices.empty()) session_->RemovePlaylistItems(std::move(indices));
        } else throw std::invalid_argument("Unsupported playlist action");
    } catch (...) {
        reject(L"插件播放列表操作参数无效，操作未提交。");
    }
}
}
