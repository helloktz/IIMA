// IINA LogWindowController.updatePredicate/menuCopy/save/scheduleFlush.
// Copyright © 2022 lhc. GPL-3.0; references/iina/LICENSE.
// Raw mpv diagnostics deliberately stay separate from the automatic disk log.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/AtomicTextFile.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <set>
#include <filesystem>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace {
std::string LevelName(int level) {
    switch(level) {
    case 10:return "fatal";case 20:return "error";case 30:return "warn";case 40:return "info";
    case 50:return "v";case 60:return "debug";case 70:return "trace";
    default:return std::to_string(level);
    }
}
std::string RawLogLine(iima::MpvLogRecord const& record) {
    return std::to_string(record.timestampMs)+" ["+LevelName(record.level)+"]["+record.prefix+"] "+
        record.text+(record.truncated ? " [truncated]" : "")+"\n";
}
bool ContainsText(std::string const& message,hstring const& needle) {
    if(needle.empty()) return true;
    auto value=to_hstring(message);
    return FindStringOrdinal(FIND_FROMSTART,value.c_str(),static_cast<int>(value.size()),
        needle.c_str(),static_cast<int>(needle.size()),TRUE)>=0;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireMpvDiagnosticViewer() {
    auto weak=get_weak();
    mpvLogTimer_=DispatcherQueue().CreateTimer();
    mpvLogTimer_.Interval(std::chrono::milliseconds(500));mpvLogTimer_.IsRepeating(true);
    mpvLogTimer_.Tick([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && self->MpvLogLive().IsChecked().Value()) self->RefreshMpvDiagnosticViewer();
    });
    MpvLogExpander().Expanding([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_) {self->RefreshMpvDiagnosticViewer();self->mpvLogTimer_.Start();}
    });
    MpvLogExpander().Collapsed([weak](auto const&,auto const&) {if(auto self=weak.get()) self->mpvLogTimer_.Stop();});
    MpvLogRefreshButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->RefreshMpvDiagnosticViewer();});
    auto render=[weak](auto const&,auto const&) {if(auto self=weak.get()) self->RenderMpvDiagnosticViewer();};
    MpvLogDisplayLevel().SelectionChanged(render);MpvLogPrefix().TextChanged(render);MpvLogSearch().TextChanged(render);
    MpvLogStartButton().Click([weak](auto const&,auto const&) {
        auto self=weak.get();if(!self || self->closing_ || self->pickerOpen_) return;
        static constexpr char const* levels[]{"fatal","error","warn","info","v","debug","trace"};
        auto index=self->MpvLogCaptureLevel().SelectedIndex();if(index<0 || index>=7) return;
        auto state=self->session_->SnapshotMpvLogs();if(state.pending || state.capturing || state.closed) return;
        self->ResetMpvDiagnosticView();
        bool accepted=self->session_->StartMpvLogCapture(levels[index]);
        self->RefreshMpvDiagnosticViewer();
        if(!accepted) self->ShowError(L"原始日志采集请求未提交。请等待播放器就绪后重试。");
    });
    MpvLogStopButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_) {self->session_->StopMpvLogCapture();self->RefreshMpvDiagnosticViewer();}
    });
    MpvLogClearButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_) {
            self->ResetMpvDiagnosticView();self->session_->ClearMpvLogCapture();self->RefreshMpvDiagnosticViewer();
        }
    });
    MpvLogCopyButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->CopyMpvDiagnosticSelection();});
    MpvLogExportButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->ExportMpvDiagnosticSnapshot();});
}
void MainWindow::ResetMpvDiagnosticView() {
    ++mpvLogEpoch_;mpvLogExportStop_.request_stop();
    if(mpvLogPicker_) {try {mpvLogPicker_.Cancel();} catch(...) {}}
    mpvLogSnapshot_={};mpvLogVisible_.clear();MpvLogList().Items().Clear();
}
void MainWindow::CloseMpvDiagnosticViewer() {
    if(mpvLogTimer_) mpvLogTimer_.Stop();
    ResetMpvDiagnosticView();
    session_->StopMpvLogCapture(); // CloseAsync also clears the Core ring.
    MpvLogPrefix().Text(L"");MpvLogSearch().Text(L"");MpvLogStatus().Text(L"");
}
void MainWindow::RefreshMpvDiagnosticViewer() {
    if(closing_) return;
    auto snapshot=session_->SnapshotMpvLogs();
    mpvLogSnapshot_=std::move(snapshot);RenderMpvDiagnosticViewer();
}
void MainWindow::RenderMpvDiagnosticViewer() {
    if(closing_) return;
    std::set<std::uint64_t> selected;
    for(auto const& item:MpvLogList().SelectedItems())
        if(auto row=item.try_as<ListViewItem>()) selected.insert(unbox_value<std::uint64_t>(row.Tag()));
    auto prefix=to_string(MpvLogPrefix().Text());auto search=MpvLogSearch().Text();
    int threshold=MpvLogDisplayLevel().SelectedIndex()*10; // 0 means all, mpv levels are 10..70.
    std::vector<iima::MpvLogRecord> visible;
    for(auto const& record:mpvLogSnapshot_.records) {
        if((threshold>0 && record.level>threshold) || (!prefix.empty() && record.prefix!=prefix) || !ContainsText(record.text,search)) continue;
        visible.push_back(record);
    }
    // Preserve native row/selection objects while appending; filters and ring
    // eviction only discard rows absent from the new bounded projection.
    std::size_t removed{};
    if(visible.empty()) removed=mpvLogVisible_.size();
    else while(removed<mpvLogVisible_.size() && mpvLogVisible_[removed].sequence<visible.front().sequence) ++removed;
    bool prefixMatches=mpvLogVisible_.size()-removed<=visible.size();
    for(std::size_t n=removed;prefixMatches && n<mpvLogVisible_.size();++n)
        prefixMatches=mpvLogVisible_[n].sequence==visible[n-removed].sequence;
    if(!prefixMatches) MpvLogList().Items().Clear();
    else for(std::size_t n=0;n<removed;++n) MpvLogList().Items().RemoveAt(0);
    for(std::size_t n=MpvLogList().Items().Size();n<visible.size();++n) {
        TextBlock text;text.Text(to_hstring(RawLogLine(visible[n])));text.TextWrapping(TextWrapping::Wrap);
        text.FontFamily(Media::FontFamily(L"Consolas"));text.FontSize(12);
        ListViewItem item;item.Content(text);item.Tag(box_value(visible[n].sequence));MpvLogList().Items().Append(item);
        if(selected.contains(visible[n].sequence)) MpvLogList().SelectedItems().Append(item);
    }
    mpvLogVisible_=std::move(visible);
    auto const& s=mpvLogSnapshot_;
    MpvLogStartButton().IsEnabled(!s.closed && !s.pending && !s.capturing && !pickerOpen_ && session_->Snapshot().session==iima::SessionState::Ready);
    MpvLogStopButton().IsEnabled(!s.closed && (s.pending || s.capturing));
    MpvLogClearButton().IsEnabled(!s.closed);
    MpvLogClearButton().Content(box_value(s.level!="no" && !s.closed ? L"清空记录（继续采集）" : L"清空本窗口原始日志"));
    MpvLogExportButton().IsEnabled(!mpvLogExportBusy_ && !pickerOpen_ && !mpvLogVisible_.empty());
    MpvLogCopyButton().IsEnabled(!mpvLogVisible_.empty());
    std::wstring status=s.closed ? L"播放器已关闭" : s.pending ? L"采集变更等待处理" : s.capturing ? L"正在采集："+std::wstring(to_hstring(s.level)) : L"采集已停止";
    status+=L"；显示 "+std::to_wstring(mpvLogVisible_.size())+L" / "+std::to_wstring(s.records.size())+
        L" 条，至少丢弃 "+std::to_wstring(s.dropped)+L"，截断 "+std::to_wstring(s.truncated)+L"。";
    if(!s.error.empty()) status+=L"\n"+std::wstring(to_hstring(s.error));
    MpvLogStatus().Text(status);
}
void MainWindow::CopyMpvDiagnosticSelection() {
    if(closing_) return;
    try {
        std::set<std::uint64_t> selected;
        for(auto const& item:MpvLogList().SelectedItems())
            if(auto row=item.try_as<ListViewItem>()) selected.insert(unbox_value<std::uint64_t>(row.Tag()));
        std::string text;
        for(auto const& record:mpvLogVisible_) if(selected.contains(record.sequence)) text+=RawLogLine(record);
        if(text.empty()) {ShowError(L"请先选择原始日志行。");return;}
        using namespace Windows::ApplicationModel::DataTransfer;
        DataPackage package;package.RequestedOperation(DataPackageOperation::Copy);package.SetText(to_hstring(text));
        ClipboardContentOptions options;options.IsAllowedInHistory(false);options.IsRoamable(false);
        if(!Clipboard::SetContentWithOptions(package,options)) {ShowError(L"剪贴板正忙，未复制。");return;}
        if(!closing_) ShowError(L"已复制选中原始日志，禁用剪贴板历史及漫游。请在分享前检查私密信息。");
    } catch(...) {if(!closing_) ShowError(L"无法复制原始日志。");}
}
fire_and_forget MainWindow::ExportMpvDiagnosticSnapshot() {
    auto lifetime=get_strong();
    if(closing_ || pickerOpen_ || mpvLogExportBusy_ || mpvLogVisible_.empty()) co_return;
    std::string text="# Raw mpv log snapshot: may contain private paths, URLs or credentials.\n";
    text+="# capture-level="+mpvLogSnapshot_.level+" display-through="+
        (MpvLogDisplayLevel().SelectedIndex()>0 ? LevelName(MpvLogDisplayLevel().SelectedIndex()*10) : std::string("all"))+"\n";
    text+="# dropped-at-least="+std::to_string(mpvLogSnapshot_.dropped)+" truncated="+std::to_string(mpvLogSnapshot_.truncated)+"\n";
    for(auto const& record:mpvLogVisible_) text+=RawLogLine(record);
    const auto epoch=mpvLogEpoch_;
    mpvLogExportStop_=std::stop_source{};auto stop=mpvLogExportStop_.get_token();
    pickerOpen_=true;mpvLogExportBusy_=true;RenderMpvDiagnosticViewer();UpdateInitialWindow();
    bool written{},failed{};
    try {
        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd{};check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.SuggestedFileName(L"IIMA-private-mpv-log");
        picker.FileTypeChoices().Insert(L"原始UTF-8日志（可能含私密信息）",single_threaded_vector<hstring>({L".log"}));
        auto operation=picker.PickSaveFileAsync();mpvLogPicker_=operation.as<Windows::Foundation::IAsyncInfo>();
        auto file=co_await operation;mpvLogPicker_=nullptr;
        if(file && !closing_ && epoch==mpvLogEpoch_ && !stop.stop_requested()) {
            std::filesystem::path path(file.Path().c_str());apartment_context ui;
            co_await resume_background();
            try {if(!stop.stop_requested()) {iima::WriteUtf8Atomically(path,text);written=true;}}
            catch(...) {failed=true;}
            text.clear();
            try {co_await ui;} catch(...) {co_return;}
        }
    } catch(...) {failed=!stop.stop_requested();}
    mpvLogPicker_=nullptr;
    if(!closing_ && epoch==mpvLogEpoch_) {
        if(written) ShowError(L"已写出点击导出时的筛选快照。文件可能含私密信息，请检查后再分享。");
        else if(failed) ShowError(L"原始日志导出失败；未确认写出成功。");
    }
    mpvLogExportBusy_=false;pickerOpen_=false;
    if(closing_) {FinishClose();co_return;}
    RenderMpvDiagnosticViewer();UpdateInitialWindow();OpenPendingFiles();FlushActivation();
}
}
