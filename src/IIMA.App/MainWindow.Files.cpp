#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/ScreenshotImage.h"
#include "../IIMA.Core/Screenshots/ScreenshotOptions.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireFileActions()
{
    auto weak = get_weak();
    OpenFileAlternativeItem().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenFile(false, true);
    });
    OpenFolderItem().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenFolder();
    });
    OpenFolderAlternativeItem().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenFolder(false, true);
    });
    AppendFolderItem().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenFolder(true);
    });
    OpenUrlAlternativeItem().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenNetworkUrl(true);
    });
    Root().DragOver([weak](auto const&, DragEventArgs const& args) {
        if (auto self = weak.get()) self->DragOverFiles(args);
    });
    Root().Drop([weak](auto const&, DragEventArgs const& args) { if (auto self = weak.get()) self->DropFiles(args); });
    CopyScreenshotButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->SaveScreenshot(true); });
    WireScreenshotPreview();
    ScreenshotButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->SaveScreenshot(); });

}
void MainWindow::QueueFiles(std::vector<std::string> paths, bool append, bool useWindowPolicy, bool invertWindowPolicy)
{
    if (closing_ || paths.empty()) return;
    if (activationTimer_) activationTimer_.Stop();
    activationInputs_.clear(); activationDelayElapsed_ = false;
    CancelAutomaticFiles(); pendingAutoFileMatch_ = false;
    inputStop_.request_stop();
    auto generation = ++inputGeneration_;
    UpdateNetworkOpen(session_->Snapshot());
    // New user input supersedes old unresolved/pending input, not active playback.
    pendingFiles_.clear();
    try {
    const bool singleSource = paths.size() == 1;
    inputResolving_ = true;
    std::vector<std::wstring> inputs;
    for (auto const& path : paths) inputs.emplace_back(to_hstring(path));
    auto weak = get_weak();
    inputStop_ = iima::LocalInputResolver::Resolve(std::move(inputs), DispatcherQueue(),
        [weak, generation, append, useWindowPolicy, singleSource, invertWindowPolicy](iima::LocalInputResult result) {
            if (auto self = weak.get(); self && !self->closing_ && self->inputGeneration_ == generation && !result.cancelled) {
                self->inputResolving_ = false;
                try {
                    if (!append && useWindowPolicy && self->openInputRouter_ && !result.paths.empty()) {
                        // A single folder remains one source. Splitting its expanded
                        // contents into separate windows would differ from openURLs.
                        self->openInputRouter_(std::move(result.paths),
                            self->preferences_.Boolean("alwaysOpenInNewWindow") != invertWindowPolicy,
                            singleSource || self->preferences_.Boolean("groupSimultaneousOpensInPlaylist"),
                            self->preferences_.Boolean("allowDuplicatePlayers"), result.singleExplicitMedia);
                    } else {
                        self->pendingFiles_ = std::move(result.paths);
                        self->pendingAppend_ = append;
                        self->pendingAutoFileMatch_ = !append && result.singleExplicitMedia;
                        self->OpenPendingFiles();
                    }
                } catch (hresult_error const& error) { self->ShowError(error.message()); }
                  catch (std::exception const& error) { self->ShowError(to_hstring(error.what())); }
                if (!result.warnings.empty()) {
                    std::wstring warning;
                    for (auto const& line : result.warnings) { if (!warning.empty()) warning += L"\n"; warning += line; }
                    self->ShowError(hstring{warning});
                }
            }
        });
    } catch (hresult_error const& error) { inputResolving_ = false; ShowError(error.message()); }
      catch (std::exception const& error) { inputResolving_ = false; ShowError(to_hstring(error.what())); }
}
void MainWindow::OpenPendingFiles()
{
    if (closing_ || !userDataLoaded_ || pendingFiles_.empty()) return;
    auto const& state = session_->Snapshot();
    if (state.session == iima::SessionState::Initializing) return;
    auto paths = std::move(pendingFiles_); pendingFiles_.clear();
    if (state.session != iima::SessionState::Ready || !state.compositionAvailable) {
        UpdateNetworkOpen(state);
        ShowError(L"播放内核尚不可用，请安装配套的 libmpv 后重新打开文件。"); return;
    }
    inputAssigned_ = true;
    BindNetworkOpenAttempt(paths, pendingAppend_);
    PrepareAutomaticFiles(paths, !pendingAppend_ && pendingAutoFileMatch_);
    pendingAutoFileMatch_ = false;
    session_->OpenMultiple(std::move(paths), pendingAppend_);
    UpdateNetworkOpen(session_->Snapshot());
}
winrt::fire_and_forget MainWindow::SaveScreenshot(bool clipboardOnly)
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || screenshotPostProcessing_ || session_->Snapshot().media != iima::MediaState::Loaded) co_return;
    const bool saveToFile = !clipboardOnly && preferences_.Boolean("screenshotSaveToFile");
    const bool copyToClipboard = clipboardOnly || preferences_.Boolean("screenshotCopyToClipboard");
    if (!saveToFile && !copyToClipboard) {
        ShowError(L"截图保存和复制均已关闭，请先在高级设置启用至少一种输出。"); co_return;
    }
    pickerOpen_ = true;
    auto entry = session_->Snapshot().entryId;
    const auto epoch = navigationEpoch_;
    // Clipboard-only output stays lossless and uses the built-in PNG decoder;
    // saved formats may require optional mpv encoders and installed WIC codecs.
    const auto format = !saveToFile ? iima::ScreenshotFormat::Png :
        static_cast<iima::ScreenshotFormat>(preferences_.Integer("screenShotFormat"));
    const auto extension = iima::ScreenshotExtension(format);
    try {
        if (extension.empty()) throw hresult_invalid_argument(L"截图格式无效。");
        std::optional<std::filesystem::path> destination;
        if (saveToFile) {
            Windows::Storage::Pickers::FileSavePicker picker;
            HWND hwnd{};
            check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            picker.SuggestedFileName(to_hstring(iima::SuggestedScreenshotFilename(
                session_->Snapshot().path, GetTickCount64(), format)));
            picker.FileTypeChoices().Insert(to_hstring(extension) + L" 图像",
                single_threaded_vector<hstring>({L"." + to_hstring(extension)}));
            auto file = co_await picker.PickSaveFileAsync();
            if (!file || closing_) { pickerOpen_ = false; co_return; }
            destination = std::filesystem::path(file.Path().c_str());
            auto suffix = to_string(destination->extension().native());
            for (auto& c : suffix) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            if (iima::ParseScreenshotFormat(suffix) != std::optional{format})
                throw hresult_invalid_argument(L"保存文件后缀与所选截图格式不一致，未生成截图。");
        }
        if (!closing_ && session_->Snapshot().entryId == entry && navigationEpoch_ == epoch) {
            auto staging = std::make_shared<iima::ScreenshotStaging>(iima::ScreenshotStaging::Create(to_hstring(extension)));
            auto weak = get_weak();
            session_->ScreenshotToFile(to_string(staging->Path().wstring()),
                [weak, staging, destination, copyToClipboard](bool success, std::string error) {
                    if (auto self = weak.get(); self && !self->closing_) {
                        if (success) self->FinishScreenshot(staging, destination, copyToClipboard);
                        else self->ShowError(to_hstring(error));
                    }
                    // staging cannot be destroyed before the actual mpv reply
                    // (or completed shutdown), even if the window has closed.
                });
        } else if (!closing_) ShowError(L"媒体已切换，未保存截图，请重新截图。");
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
      catch (std::exception const& error) { if (!closing_) ShowError(to_hstring(error.what())); }
    pickerOpen_ = false;
}
winrt::fire_and_forget MainWindow::FinishScreenshot(std::shared_ptr<iima::ScreenshotStaging> staging,
    std::optional<std::filesystem::path> destination, bool copyToClipboard) {
    auto lifetime = get_strong();
    screenshotPostProcessing_ = true;
    apartment_context ui;
    std::optional<iima::ScreenshotPixels> pixels;
    std::optional<iima::ScreenshotPixels> preview;
    const bool showPreview = preferences_.Boolean("screenshotShowPreview") && preferences_.Boolean("enableOSD");
    std::exception_ptr saveFailure, copyFailure;
    bool saved{}, copied{}, previewFailed{};
    co_await resume_background();
    auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (destination) {
        try { staging->Publish(*destination); saved = true; }
        catch (...) { saveFailure = std::current_exception(); }
    }
    // File publication and decoding/clipboard are independent outputs. A full
    // destination drive must not prevent copying the already captured image.
    if (copyToClipboard || showPreview) {
        try {
            check_hresult(initialized);
            auto decoded = iima::DecodeScreenshot(staging->Path());
            if (showPreview) {
                try { preview = iima::MakeScreenshotPreview(decoded); }
                catch (...) { previewFailed = true; }
            }
            if (copyToClipboard) pixels = std::move(decoded);
        } catch (...) {
            if (copyToClipboard) copyFailure = std::current_exception();
            if (showPreview) previewFailed = true;
        }
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    try { co_await ui; } catch (...) { co_return; }
    screenshotPostProcessing_ = false;
    if (closing_) { FinishClose(); co_return; }
    if (pixels) {
        try {
            HWND hwnd{};
            check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
            iima::CopyScreenshotToClipboard(hwnd, *pixels);
            copied = true;
        } catch (...) { copyFailure = std::current_exception(); }
    }
    if (preview && preferences_.Boolean("screenshotShowPreview") && preferences_.Boolean("enableOSD")) {
        // Never expose a nonexistent/unpublished destination via preview actions.
        try { ShowScreenshotPreview(*preview, saved ? destination : std::nullopt); }
        catch (...) { previewFailed = true; HideScreenshotPreview(); }
    }
    auto detail = [](std::exception_ptr failure) -> hstring {
        if (!failure) return {};
        try { std::rethrow_exception(failure); }
        catch (hresult_error const& error) { return error.message(); }
        catch (std::exception const& error) { return to_hstring(error.what()); }
        catch (...) { return L"未知错误"; }
    };
    hstring report;
    if (destination) report = saved ? L"截图已保存。" : L"截图保存失败：" + detail(saveFailure) + L"。";
    if (copyToClipboard) report = report + (copied ? hstring{L"已复制到剪贴板。"} :
        L"复制失败：" + detail(copyFailure) + L"。");
    if (previewFailed) report = report + L"预览不可用。";
    // Partial success is final for this request: no automatic retry that could
    // overwrite a saved file or another application's newer clipboard contents.
    ShowError(report);
}
}
