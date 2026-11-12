// History ordering/defaults follow IINA HistoryController.swift and Preference.swift.
// Copyright © 2017 lhc. Adapted logic: GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/UserDataStore.h"
#include <chrono>
#include <thread>
#include <winrt/Microsoft.UI.Dispatching.h>

using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::LoadUserData()
{
    auto weak = get_weak();
    auto dispatcher = DispatcherQueue();
    std::thread([weak, dispatcher] {
        iima::UserData data;
        bool initialized = false;
        try {
            init_apartment(apartment_type::multi_threaded); initialized = true;
            data = iima::UserDataStore::Load();
        } catch (std::exception const& e) { data.warning = e.what(); data.writable = false; }
          catch (hresult_error const& e) { data.warning = to_string(e.message()); data.writable = false; }
        if (initialized) uninit_apartment();
        dispatcher.TryEnqueue([weak, data = std::move(data)]() mutable {
            if (auto self = weak.get()) {
                self->userData_ = std::move(data);
                self->userDataLoaded_ = true;
                if (!self->closing_) self->Root().IsEnabled(true);
                self->updating_ = true;
                self->RecordHistoryCheck().IsChecked(self->userData_.settings.recordHistory);
                self->ResumeCheck().IsChecked(self->userData_.settings.resumePosition);
                self->updating_ = false;
                self->RefreshHistory();
                self->RefreshUserEqualizers();
                self->ApplyKeyboardConfiguration();
                self->LoadSavedFilters();
                self->preferences_.Restore(self->userData_.settings.advancedPreferences);
                self->PopulateAdvancedSettings();
                self->ApplyWindowPreferences();
                self->ApplyUserData(self->session_->Snapshot());
                self->OpenPendingFiles();
                self->FlushActivation();
                if (!self->userData_.warning.empty()) self->ShowError(to_hstring(self->userData_.warning));
                if (self->closing_) {
                    if (!self->userData_.writable && !self->discardUserData_) self->ConfirmUnsavedClose();
                    else { self->saveDirty_ = true; self->PumpSave(); }
                }
            }
        });
    }).detach();
}
void MainWindow::WireUserDataControls()
{
    auto weak = get_weak();
    auto changed = [weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->updating_ && !self->closing_) {
            self->userData_.settings.recordHistory = self->RecordHistoryCheck().IsChecked().Value();
            self->userData_.settings.resumePosition = self->ResumeCheck().IsChecked().Value();
            self->MarkUserDataDirty();
        }
    };
    RecordHistoryCheck().Checked(changed); RecordHistoryCheck().Unchecked(changed);
    ResumeCheck().Checked(changed); ResumeCheck().Unchecked(changed);
    WireHistoryControls();
}
void MainWindow::ApplyUserData(iima::PlaybackSnapshot const& state)
{
    if (!userDataLoaded_) return;
    // Reloading the same playlist entry is a new open even though its ID is
    // unchanged. Re-arm the fullscreen preference at the loading boundary.
    if (state.media == iima::MediaState::Loading || state.media == iima::MediaState::Empty)
        fullscreenPreferenceEntry_.reset();
    if (!settingsApplied_ && state.session == iima::SessionState::Ready && !closing_) {
        settingsApplied_ = true;
        session_->ApplySettings(userData_.settings);
        session_->ApplyAdvancedPreferences(preferences_);
    }
    if (state.media == iima::MediaState::Loading || state.media == iima::MediaState::Empty ||
        (lastMedia_ && (lastMedia_->entryId != state.entryId || lastMedia_->path != state.path))) {
        RecordLastMedia(); lastMedia_.reset(); resumedEntry_.reset();
    }
    if (state.media == iima::MediaState::Loaded || state.media == iima::MediaState::Ended) {
        if (!resumedEntry_ && !closing_ && presenter_ && presenter_->IsBound() &&
            !state.path.empty() && state.duration && *state.duration > 0) {
            resumedEntry_ = state.entryId;
            if (auto position = iima::ResumePosition(userData_, state.path)) session_->Seek(*position);
        }
        // Retain only history fields, not an entire playlist or GPU ownership.
        lastMedia_ = HistorySample{state.entryId, state.path, state.position, state.duration};
    }
}
void MainWindow::RecordLastMedia()
{
    if (!userDataLoaded_ || !lastMedia_ || !lastMedia_->position || lastMedia_->path.empty()) return;
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    iima::RecordHistory(userData_, {lastMedia_->path, *lastMedia_->position, lastMedia_->duration, timestamp});
    RefreshHistory(); MarkUserDataDirty();
}
void MainWindow::MarkUserDataDirty()
{
    if (!userDataLoaded_) return;
    saveDirty_ = true;
    PumpSave();
}
void MainWindow::PumpSave()
{
    if (!userDataLoaded_ || !userData_.writable || discardUserData_ || saving_ || !saveDirty_ || saveFailed_) { FinishClose(); return; }
    saving_ = true; saveDirty_ = false;
    auto data = userData_;
    auto dispatcher = DispatcherQueue(); auto weak = get_weak();
    std::thread([weak, dispatcher, data = std::move(data)] {
        std::string error;
        bool initialized = false;
        try {
            init_apartment(apartment_type::multi_threaded); initialized = true;
            iima::UserDataStore::Save(data);
        } catch (std::exception const& e) { error = e.what(); }
          catch (hresult_error const& e) { error = to_string(e.message()); }
        if (initialized) uninit_apartment();
        dispatcher.TryEnqueue([weak, error = std::move(error)] {
            if (auto self = weak.get()) {
                self->saving_ = false;
                if (!error.empty()) {
                    self->saveDirty_ = true; self->saveFailed_ = true;
                    self->ShowError(L"设置或历史保存失败：" + to_hstring(error) + L"。本次更改尚未保存。");
                    if (self->closing_) self->ConfirmUnsavedClose();
                } else self->PumpSave();
            }
        });
    }).detach();
}
winrt::fire_and_forget MainWindow::ConfirmUnsavedClose()
{
    auto lifetime = get_strong();
    if (saveDialogOpen_) co_return;
    saveDialogOpen_ = true;
    try {
        Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(L"设置或历史尚未保存"));
        dialog.Content(box_value(userData_.writable
            ? L"保存失败。可以重试，或退出并放弃本次未保存的更改。"
            : L"原数据未能安全读取或备份。为保护原文件，本次不会覆盖保存；退出会放弃本次更改。"));
        dialog.PrimaryButtonText(L"重试保存");
        dialog.IsPrimaryButtonEnabled(userData_.writable);
        dialog.SecondaryButtonText(L"退出而不保存");
        dialog.CloseButtonText(L"留在窗口");
        auto result = co_await dialog.ShowAsync();
        saveDialogOpen_ = false;
        if (result == Microsoft::UI::Xaml::Controls::ContentDialogResult::Secondary) {
            discardUserData_ = true;
            if (closing_) FinishClose(); else BeginClose();
        } else if (result == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary) {
            saveFailed_ = false;
            if (closing_) PumpSave(); else BeginClose();
        }
    } catch (hresult_error const& error) { saveDialogOpen_ = false; ShowError(error.message()); }
}
void MainWindow::FinishClose()
{
    if (closing_ && !screenshotPostProcessing_ && coreClosed_ && diagnosticsClosed_ && userDataLoaded_ && !saving_ && (discardUserData_ || (!saveDirty_ && !saveFailed_ && userData_.writable))) {
        closeComplete_ = true; Close();
    }
}
}
