// IINA LogWindowController.updatePredicate/menuCopy/save/scheduleFlush.
// Copyright (c) 2022 lhc and IINA contributors. GPL-3.0-or-later.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/AtomicTextFile.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <chrono>
#include <set>
#include <exception>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace {
std::string LogLine(iima::DiagnosticLogRecord const& record) {
    return std::to_string(record.timestampMs) + " [" + record.category + "] " + record.message + "\n";
}
bool MessageContains(std::string const& message, hstring const& search) {
    if (search.empty()) return true;
    auto wide = to_hstring(message);
    return FindStringOrdinal(FIND_FROMSTART, wide.c_str(), static_cast<int>(wide.size()),
        search.c_str(), static_cast<int>(search.size()), TRUE) >= 0;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireDiagnosticViewer() {
    auto weak = get_weak();
    diagnosticTimer_ = DispatcherQueue().CreateTimer();
    diagnosticTimer_.Interval(std::chrono::milliseconds(500));
    diagnosticTimer_.IsRepeating(true);
    diagnosticTimer_.Tick([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->DiagnosticLiveBox().IsChecked().Value())
            self->RefreshDiagnosticViewer(false);
    });
    DiagnosticExpander().Expanding([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            // Force a fresh snapshot even when live updates are disabled. The
            // first timer tick occurs after the Expander has set IsExpanded.
            self->RefreshDiagnosticViewer(true);
            self->diagnosticTimer_.Start();
        }
    });
    DiagnosticExpander().Collapsed([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->diagnosticTimer_.Stop();
    });
    DiagnosticRefreshButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RefreshDiagnosticViewer(true);
    });
    DiagnosticSearchBox().TextChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RenderDiagnosticViewer();
    });
    DiagnosticCategoryBox().TextChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RenderDiagnosticViewer();
    });
    DiagnosticCopyButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CopyDiagnosticSelection();
    });
    DiagnosticSaveFilteredButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SaveDiagnosticSnapshot(false);
    });
    DiagnosticSaveAllButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SaveDiagnosticSnapshot(true);
    });
}
void MainWindow::RefreshDiagnosticViewer(bool force) {
    if (closing_ || (!force && !DiagnosticExpander().IsExpanded())) return;
    if (!diagnostics_) { DiagnosticStatusText().Text(L"本窗口诊断日志不可用。"); return; }
    // Bounded owned memory only. No disk I/O and no mpv read on the UI thread.
    auto snapshot = diagnostics_->Snapshot();
    if (!force && snapshot.revision == diagnosticSnapshot_.revision &&
        snapshot.diskQueueDropped == diagnosticSnapshot_.diskQueueDropped) return;
    diagnosticSnapshot_ = std::move(snapshot);
    RenderDiagnosticViewer();
}
void MainWindow::RenderDiagnosticViewer() {
    if (closing_) return;
    std::set<std::uint64_t> selected;
    for (auto const& item : DiagnosticList().SelectedItems()) {
        if (auto row = item.try_as<ListViewItem>()) selected.insert(unbox_value<std::uint64_t>(row.Tag()));
    }
    auto category = to_string(DiagnosticCategoryBox().Text());
    auto search = DiagnosticSearchBox().Text();
    std::vector<iima::DiagnosticLogRecord> visible;
    for (auto const& record : diagnosticSnapshot_.records) {
        // An empty category means all. Exact category matching follows the
        // upstream subsystem predicate; message search is case-insensitive.
        if ((!category.empty() && category != record.category) || !MessageContains(record.message, search)) continue;
        visible.push_back(record);
    }
    // Preserve row objects (and the user's scroll/selection) for ordinary appends.
    // Filter changes may need a rebuild; ring eviction removes only expired rows.
    std::size_t removed{};
    if (!visible.empty()) {
        while (removed < diagnosticVisible_.size() && diagnosticVisible_[removed].sequence < visible.front().sequence) ++removed;
    } else removed = diagnosticVisible_.size();
    bool samePrefix = diagnosticVisible_.size() - removed <= visible.size();
    for (std::size_t n = removed; samePrefix && n < diagnosticVisible_.size(); ++n)
        samePrefix = diagnosticVisible_[n].sequence == visible[n - removed].sequence;
    if (!samePrefix) DiagnosticList().Items().Clear();
    else for (std::size_t n = 0; n < removed; ++n) DiagnosticList().Items().RemoveAt(0);
    auto existing = DiagnosticList().Items().Size();
    for (std::size_t n = existing; n < visible.size(); ++n) {
        auto const& record = visible[n];
        TextBlock text;
        text.Text(to_hstring(LogLine(record)));
        text.FontFamily(Media::FontFamily(L"Consolas"));
        text.FontSize(12);
        text.TextWrapping(TextWrapping::Wrap);
        ListViewItem row;
        row.Content(text); row.Tag(box_value(record.sequence));
        DiagnosticList().Items().Append(row);
        if (selected.contains(record.sequence)) DiagnosticList().SelectedItems().Append(row);
    }
    diagnosticVisible_ = std::move(visible);
    if (DiagnosticFollowBox().IsChecked().Value() && DiagnosticList().Items().Size())
        DiagnosticList().ScrollIntoView(DiagnosticList().Items().GetAt(DiagnosticList().Items().Size() - 1));
    DiagnosticStatusText().Text(L"显示 " + to_hstring(diagnosticVisible_.size()) + L" / " +
        to_hstring(diagnosticSnapshot_.records.size()) + L" 条；内存淘汰 " + to_hstring(diagnosticSnapshot_.evicted) +
        L" 条；磁盘排队丢弃 " + to_hstring(diagnosticSnapshot_.diskQueueDropped) + L" 条。时间为 Unix 毫秒。");
}
void MainWindow::CopyDiagnosticSelection() {
    if (closing_) return;
    try {
        std::set<std::uint64_t> selected;
        for (auto const& item : DiagnosticList().SelectedItems())
            if (auto row = item.try_as<ListViewItem>()) selected.insert(unbox_value<std::uint64_t>(row.Tag()));
        std::string text;
        for (auto const& record : diagnosticVisible_)
            if (selected.contains(record.sequence)) text += LogLine(record);
        if (text.empty()) { DiagnosticStatusText().Text(L"请先选择要复制的日志行。"); return; }
        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(to_hstring(text));
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        DiagnosticStatusText().Text(L"已将选中日志放入剪贴板。");
    } catch (...) { DiagnosticStatusText().Text(L"无法写入剪贴板，请重试。"); }
}
fire_and_forget MainWindow::SaveDiagnosticSnapshot(bool all) {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    // Export exactly the displayed snapshot, not new entries arriving while the
    // save picker is open. 'All' means retained snapshot, never archived files.
    std::string text = "# IIMA bounded diagnostic snapshot; timestamps are Unix milliseconds.\n";
    text += "# retained=" + std::to_string(diagnosticSnapshot_.records.size()) +
        " evicted=" + std::to_string(diagnosticSnapshot_.evicted) +
        " disk-queue-dropped=" + std::to_string(diagnosticSnapshot_.diskQueueDropped) + "\n";
    for (auto const& record : all ? diagnosticSnapshot_.records : diagnosticVisible_) text += LogLine(record);
    pickerOpen_ = true;
    apartment_context ui;
    try {
        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd{};
        check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.SuggestedFileName(all ? L"IIMA-diagnostic-snapshot" : L"IIMA-diagnostic-filtered");
        picker.FileTypeChoices().Insert(L"UTF-8 日志", single_threaded_vector<hstring>({L".log"}));
        auto file = co_await picker.PickSaveFileAsync();
        if (file && !closing_) {
            auto path = std::filesystem::path(file.Path().c_str());
            co_await resume_background();
            std::exception_ptr failure;
            try { iima::WriteUtf8Atomically(path, text); }
            catch (...) { failure = std::current_exception(); }
            co_await ui;
            if (failure) std::rethrow_exception(failure);
            if (!closing_) DiagnosticStatusText().Text(L"已保存操作开始时的日志快照。");
        }
    } catch (...) { if (!closing_) ShowError(L"保存诊断日志失败。"); }
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}
