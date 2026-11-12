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
    WireScreenshotSettings();
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
}
