#include "pch.h"
#include "MainWindow.xaml.h"
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
    Root().DragOver([](auto const&, DragEventArgs const& args) {
        if (args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
    });
    Root().Drop([weak](auto const&, DragEventArgs const& args) { if (auto self = weak.get()) self->DropFiles(args); });
    ScreenshotButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->SaveScreenshot(); });
    Input::KeyboardAccelerator screenshot;
    screenshot.Key(Windows::System::VirtualKey::S);
    screenshot.Modifiers(Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Shift);
    screenshot.Invoked([weak](auto const&, auto const& args) {
        if (auto self = weak.get()) self->SaveScreenshot();
        args.Handled(true);
    });
    Root().KeyboardAccelerators().Append(screenshot);
}
void MainWindow::QueueFiles(std::vector<std::string> paths, bool append)
{
    if (closing_ || paths.empty()) return;
    if (activationTimer_) activationTimer_.Stop();
    activationInputs_.clear(); activationDelayElapsed_ = false;
    inputStop_.request_stop();
    auto generation = ++inputGeneration_;
    // New user input supersedes old unresolved/pending input, not active playback.
    pendingFiles_.clear();
    try {
    std::vector<std::wstring> inputs;
    for (auto const& path : paths) inputs.emplace_back(to_hstring(path));
    auto weak = get_weak();
    inputStop_ = iima::LocalInputResolver::Resolve(std::move(inputs), DispatcherQueue(),
        [weak, generation, append](iima::LocalInputResult result) {
            if (auto self = weak.get(); self && !self->closing_ && self->inputGeneration_ == generation && !result.cancelled) {
                self->pendingFiles_ = std::move(result.paths);
                self->pendingAppend_ = append;
                self->OpenPendingFiles();
                if (!result.warnings.empty()) {
                    std::wstring warning;
                    for (auto const& line : result.warnings) { if (!warning.empty()) warning += L"\n"; warning += line; }
                    self->ShowError(hstring{warning});
                }
            }
        });
    } catch (hresult_error const& error) { ShowError(error.message()); }
      catch (std::exception const& error) { ShowError(to_hstring(error.what())); }
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
winrt::fire_and_forget MainWindow::SaveScreenshot()
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || session_->Snapshot().media != iima::MediaState::Loaded) co_return;
    pickerOpen_ = true;
    // Do not take a different file's screenshot if media changes while the dialog is open.
    auto entry = session_->Snapshot().entryId;
    try {
        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.SuggestedFileName(L"IIMA-screenshot");
        picker.FileTypeChoices().Insert(L"PNG 图像", single_threaded_vector<hstring>({L".png"}));
        auto file = co_await picker.PickSaveFileAsync();
        if (file && !closing_) {
            if (session_->Snapshot().entryId == entry) session_->ScreenshotToFile(to_string(file.Path()));
            else ShowError(L"媒体已切换，未保存截图，请重新截图。");
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    pickerOpen_ = false;
}
}
