// IINA HistoryWindowController.reloadData / selectedPlayableEntries, asynchronous
// Windows status projection. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/HistoryAvailability.h"
#include <algorithm>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireHistoryAvailability() {
    auto weak = get_weak();
    RefreshHistoryAvailabilityButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RequestHistoryAvailability(true);
    });
    CancelHistoryAvailabilityButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->historyAvailabilityBusy_) {
            self->historyAvailabilityPending_ = false;
            self->historyAvailabilityStop_.request_stop();
            self->HistoryAvailabilityStatus().Text(L"已请求停止检查；正在进行的Windows文件查询可能仍需返回。");
        }
    });
}
void MainWindow::RequestHistoryAvailability(bool force) {
    if (closing_ || !userDataLoaded_) return;
    std::vector<std::string> paths;
    paths.reserve(userData_.history.size());
    for (auto const& entry : userData_.history) paths.push_back(entry.path);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (!force && historyAvailabilityRequested_ && paths == historyAvailabilityPaths_) return;
    historyAvailabilityRequested_ = true;
    historyAvailabilityPaths_ = std::move(paths);
    if (historyAvailabilityBusy_) {
        historyAvailabilityPending_ = true;
        historyAvailabilityStop_.request_stop();
        return; // At most one native worker; no unbounded cancelled-job pileup.
    }
    historyAvailabilityPending_ = false;
    ScanHistoryAvailabilityAsync(historyAvailabilityPaths_);
}
fire_and_forget MainWindow::ScanHistoryAvailabilityAsync(std::vector<std::string> paths) {
    auto lifetime = get_strong();
    historyAvailabilityBusy_ = true;
    historyAvailabilityStop_ = std::stop_source{};
    auto stop = historyAvailabilityStop_.get_token();
    CancelHistoryAvailabilityButton().IsEnabled(true);
    HistoryAvailabilityStatus().Text(L"后台检查本地历史文件…网络、不可访问及不支持路径保持未知。");
    apartment_context ui;
    iima::HistoryAvailabilityScan result;
    bool failed{};
    co_await resume_background();
    try { result = iima::ScanHistoryAvailability(paths, stop); }
    catch (...) { failed = true; }
    try { co_await ui; } catch (...) { co_return; }
    historyAvailabilityBusy_ = false;
    if (closing_) co_return;
    CancelHistoryAvailabilityButton().IsEnabled(false);
    if (!failed && !stop.stop_requested() && paths == historyAvailabilityPaths_) {
        historyAvailability_ = std::move(result.entries);
        std::size_t available{}, missing{};
        for (auto const& [path, value] : historyAvailability_) {
            if (value == iima::HistoryAvailability::Available) ++available;
            else if (value == iima::HistoryAvailability::Missing) ++missing;
        }
        HistoryAvailabilityStatus().Text(L"上次检查：存在 " + to_hstring(available) + L"，缺失 " +
            to_hstring(missing) + L"，未知 " + to_hstring(paths.size() - available - missing) +
            L"。外部文件变化后请刷新。" + (result.limited ? L"达到数量/时间预算，未检查部分保持未知。" : L""));
        RefreshHistory(); // Same path snapshot does not schedule another scan.
    } else if (!historyAvailabilityPending_) {
        HistoryAvailabilityStatus().Text(failed ? L"检查失败，保留上次状态；未知不代表文件缺失。" :
            L"检查已停止，保留上次状态。可手动刷新。");
    }
    if (historyAvailabilityPending_) {
        historyAvailabilityPending_ = false;
        RequestHistoryAvailability(true);
    }
}
}
