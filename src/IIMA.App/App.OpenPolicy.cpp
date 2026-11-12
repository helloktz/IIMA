// IINA PlayerCore.openURLs, newPlayerCore and findIdlePlayerCore.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <set>

namespace winrt::IIMA::implementation {
void App::RouteResolvedInput(WindowId source, std::vector<std::string> paths,
                            bool newWindow, bool group, bool allowDuplicates, bool singleExplicitMedia)
{
    auto origin = windows_.find(source);
    if (origin == windows_.end() || origin->second->IsClosing() || paths.empty()) return;
    if (!newWindow) {
        origin->second->ReceiveResolvedInput(std::move(paths), singleExplicitMedia);
        return;
    }
    struct Delivery {
        winrt::com_ptr<MainWindow> window;
        std::vector<std::string> paths;
    };
    std::vector<Delivery> deliveries;
    std::set<WindowId> reserved;
    auto reserveIdle = [&]() -> winrt::com_ptr<MainWindow> {
        // Creation order matches IINA's first idle core. Pending activation,
        // input resolution, picker, and already assigned windows are not idle.
        for (auto const& [id, window] : windows_) {
            if (!reserved.contains(id) && window->CanReuseForInput()) {
                reserved.insert(id);
                return window;
            }
        }
        return {};
    };
    if (paths.size() > 1 && group) {
        // Deliberately bypass duplicate suppression for a grouped batch, as IINA
        // does. A duplicate file is still a legitimate playlist entry.
        deliveries.push_back({reserveIdle(), std::move(paths)});
    } else {
        for (auto& path : paths) {
            if (!allowDuplicates) {
                auto planned = std::find_if(deliveries.begin(), deliveries.end(), [&](auto const& d) {
                    return !d.paths.empty() && d.paths.front() == path;
                });
                if (planned != deliveries.end()) continue;
                auto existing = std::find_if(windows_.begin(), windows_.end(), [&](auto const& pair) {
                    return !pair.second->IsClosing() && pair.second->HasOpenInput(path);
                });
                if (existing != windows_.end()) {
                    reserved.insert(existing->first);
                    // Empty delivery activates without reopening or seeking.
                    deliveries.push_back({existing->second, {}});
                    continue;
                }
            }
            deliveries.push_back({reserveIdle(), {std::move(path)}});
        }
    }
    const auto needed = static_cast<std::size_t>(std::count_if(deliveries.begin(), deliveries.end(),
        [](auto const& d) { return !d.window; }));
    if (windows_.size() + needed > iima::PlayerWindowRegistry::WindowLimit)
        throw winrt::hresult_error(E_OUTOFMEMORY,
            L"此批打开请求需要超过 16 个窗口。请启用 groupSimultaneousOpensInPlaylist 或减少选择；本批未打开。");
    // Allocate all windows before replacing any media. Failure may leave newly
    // created empty windows, but does not silently open only the first N files.
    for (auto& delivery : deliveries)
        if (!delivery.window) delivery.window = CreatePlayerWindow();
    for (auto& delivery : deliveries) {
        if (delivery.paths.empty()) delivery.window->ReceiveActivation({});
        else delivery.window->ReceiveResolvedInput(std::move(delivery.paths), singleExplicitMedia);
    }
}
}
