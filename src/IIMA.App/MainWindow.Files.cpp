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
    Root().DragOver([](auto const&, DragEventArgs const& args) {
        if (args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
    });
    Root().Drop([weak](auto const&, DragEventArgs const& args) { if (auto self = weak.get()) self->DropFiles(args); });
    CopyScreenshotButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->SaveScreenshot(true); });
    ScreenshotButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->SaveScreenshot(); });

}
void MainWindow::QueueFiles(std::vector<std::string> paths, bool append, bool useWindowPolicy, bool invertWindowPolicy)
{
    if (closing_ || paths.empty()) return;
    if (activationTimer_) activationTimer_.Stop();
    activationInputs_.clear(); activationDelayElapsed_ = false;
    inputStop_.request_stop();
    auto generation = ++inputGeneration_;
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
                            self->preferences_.Boolean("allowDuplicatePlayers"));
                    } else {
                        self->pendingFiles_ = std::move(result.paths);
                        self->pendingAppend_ = append;
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
        ShowError(L"播放内核尚不可用，请安装配套的 libmpv 后重新打开文件。"); return;
    }
    inputAssigned_ = true;
    session_->OpenMultiple(std::move(paths), pendingAppend_);
}
winrt::fire_and_forget MainWindow::DropFiles(DragEventArgs args)
{
    auto lifetime = get_strong();
    if (closing_) co_return;
    auto deferral = args.GetDeferral();
    try {
        auto items = co_await args.DataView().GetStorageItemsAsync();
        std::vector<std::string> paths;
        for (auto const& item : items) paths.push_back(to_string(item.Path()));
        if (!closing_) QueueFiles(std::move(paths));
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    deferral.Complete();
}
winrt::fire_and_forget MainWindow::SaveScreenshot(bool clipboardOnly)
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || screenshotPostProcessing_ || session_->Snapshot().media != iima::MediaState::Loaded) co_return;
    pickerOpen_ = true;
    auto entry = session_->Snapshot().entryId;
    try {
        std::optional<std::filesystem::path> destination;
        if (!clipboardOnly) {
            Windows::Storage::Pickers::FileSavePicker picker;
            HWND hwnd{};
            check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            picker.SuggestedFileName(to_hstring(iima::SuggestedScreenshotFilename(
                session_->Snapshot().path, GetTickCount64(), iima::ScreenshotFormat::Png)));
            picker.FileTypeChoices().Insert(L"PNG 图像", single_threaded_vector<hstring>({L".png"}));
            auto file = co_await picker.PickSaveFileAsync();
            if (!file || closing_) { pickerOpen_ = false; co_return; }
            destination = std::filesystem::path(file.Path().c_str());
        }
        if (!closing_ && session_->Snapshot().entryId == entry) {
            auto staging = std::make_shared<iima::ScreenshotStaging>(iima::ScreenshotStaging::Create());
            auto weak = get_weak();
            session_->ScreenshotToFile(to_string(staging->Path().wstring()),
                [weak, staging, destination, clipboardOnly](bool success, std::string error) {
                    if (auto self = weak.get(); self && !self->closing_) {
                        if (success) self->FinishScreenshot(staging, destination, clipboardOnly);
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
    std::exception_ptr failure;
    bool previewFailed = false;
    co_await resume_background();
    auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        if (destination) staging->Publish(*destination);
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
                if (copyToClipboard) throw;
                previewFailed = true; // A decode failure does not undo a saved PNG.
            }
        }
    } catch (...) { failure = std::current_exception(); }
    if (SUCCEEDED(initialized)) CoUninitialize();
    co_await ui;
    screenshotPostProcessing_ = false;
    if (closing_) { FinishClose(); co_return; }
    try {
        if (failure) std::rethrow_exception(failure);
        if (pixels) {
            HWND hwnd{};
            check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
            iima::CopyScreenshotToClipboard(hwnd, *pixels);
        }
        if (preview && preferences_.Boolean("screenshotShowPreview") && preferences_.Boolean("enableOSD")) {
            try { ShowScreenshotPreview(*preview); }
            catch (...) { previewFailed = true; HideScreenshotPreview(); }
        }
        ShowError(previewFailed ? (copyToClipboard ? L"截图已复制，但预览不可用。" : L"截图已保存，但预览不可用。") :
            (copyToClipboard ? L"截图已复制到剪贴板。" : L"截图已保存。"));
    } catch (hresult_error const& error) { ShowError(error.message()); }
      catch (std::exception const& error) { ShowError(to_hstring(error.what())); }
}
}
