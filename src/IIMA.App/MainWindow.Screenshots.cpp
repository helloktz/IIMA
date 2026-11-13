// IINA PlayerCore.screenshotCallback and screenshot folder/template preferences.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
fire_and_forget MainWindow::SaveScreenshot(bool clipboardOnly, bool forceSaveAs) {
    auto lifetime=get_strong();
    if(closing_ || !userDataLoaded_ || pickerOpen_ || screenshotPostProcessing_ ||
        session_->Snapshot().media!=iima::MediaState::Loaded) co_return;
    ScreenshotOutputRequest request;
    request.save=forceSaveAs || (!clipboardOnly && preferences_.Boolean("screenshotSaveToFile"));
    request.copy=clipboardOnly || preferences_.Boolean("screenshotCopyToClipboard");
    request.preview=preferences_.Boolean("screenshotShowPreview") && preferences_.Boolean("enableOSD");
    request.forceSaveAs=forceSaveAs;
    if(!request.save && !request.copy) { ShowError(L"截图保存和复制均已关闭，请启用至少一种输出。"); co_return; }
    request.format=request.save ? static_cast<iima::ScreenshotFormat>(preferences_.Integer("screenShotFormat")) : iima::ScreenshotFormat::Png;
    const auto extension=iima::ScreenshotExtension(request.format);
    try {
        if(extension.empty()) throw hresult_invalid_argument(L"截图格式无效。");
        const auto mediaPath=session_->Snapshot().path;
        const auto fallbackMedia=mediaPath.find("://")!=mediaPath.npos ? std::string{"IIMA-screenshot"} : mediaPath;
        auto filename=request.save ? iima::PlanScreenshotFilename(session_->Snapshot().path,++screenshotSequence_,
            request.format,preferences_.String("screenShotTemplate")) :
            std::optional<std::string>{iima::SuggestedScreenshotFilename(fallbackMedia,++screenshotSequence_,request.format)};
        if(!filename && forceSaveAs) filename=iima::SuggestedScreenshotFilename(fallbackMedia,screenshotSequence_,request.format);
        if(!filename) throw hresult_invalid_argument(L"截图文件名模板无效，仅支持%F、%n和%%且不能包含路径。");
        request.filename=std::move(*filename);
        if(request.save && !forceSaveAs) request.directory=std::filesystem::path(to_hstring(preferences_.String("screenShotFolder")).c_str());
        auto staging=std::make_shared<iima::ScreenshotStaging>(iima::ScreenshotStaging::Create(to_hstring(extension)));
        // One bounded request covers capture, picker and publication. Capture
        // is queued before any save picker, not a promise of an exact click frame.
        screenshotPostProcessing_=true;
        pickerOpen_=true; // Reserve the modal slot until this captured image is handled.
        auto weak=get_weak();
        session_->ScreenshotToFile(to_string(staging->Path().native()),
            [weak,staging,request=std::move(request)](bool success,std::string error) mutable {
                auto self=weak.get();
                if(!self) return;
                if(!success || self->closing_) {
                    self->screenshotPostProcessing_=false; self->pickerOpen_=false;
                    if(self->closing_) self->FinishClose();
                    else { self->ShowError(to_hstring(error)); self->OpenPendingFiles(); self->FlushActivation(); }
                    return;
                }
                self->FinishScreenshot(staging,std::move(request));
            });
    } catch(hresult_error const& error) {
        screenshotPostProcessing_=false; pickerOpen_=false;
        if(!closing_) ShowError(error.message()); else FinishClose();
    } catch(...) {
        screenshotPostProcessing_=false; pickerOpen_=false;
        if(!closing_) ShowError(L"无法提交截图。"); else FinishClose();
    }
}
fire_and_forget MainWindow::FinishScreenshot(std::shared_ptr<iima::ScreenshotStaging> staging, ScreenshotOutputRequest request) {
    auto lifetime=get_strong();
    std::optional<std::filesystem::path> destination;
    std::exception_ptr saveFailure,copyFailure;
    bool saveCancelled{},saved{},copied{},previewFailed{};
    // The frame already exists in the private staging file. Choosing a location
    // cannot change it even if playback advances while the picker is visible.
    if(request.save && (request.forceSaveAs || request.directory.empty()) && !closing_) {
        try {
            Windows::Storage::Pickers::FileSavePicker picker;
            HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            auto extension=iima::ScreenshotExtension(request.format);
            picker.SuggestedFileName(to_hstring(request.filename));
            picker.FileTypeChoices().Insert(to_hstring(extension)+L" 图像",single_threaded_vector<hstring>({L"."+to_hstring(extension)}));
            auto operation=picker.PickSaveFileAsync();
            screenshotPickerOperation_=operation.as<Windows::Foundation::IAsyncInfo>();
            auto file=co_await operation;
            screenshotPickerOperation_=nullptr;
            if(file) {
                destination=std::filesystem::path(file.Path().c_str());
                auto suffix=to_string(destination->extension().native());
                for(auto& c:suffix) if(c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
                if(iima::ParseScreenshotFormat(suffix)!=std::optional{request.format})
                    throw hresult_invalid_argument(L"保存后缀与截图格式不一致，未保存文件。");
            } else saveCancelled=true;
        } catch(...) { screenshotPickerOperation_=nullptr; saveFailure=std::current_exception(); }
    }
    if(closing_) {
        screenshotPostProcessing_=false; pickerOpen_=false; FinishClose(); co_return;
    }
    apartment_context ui;
    std::optional<iima::ScreenshotPixels> pixels,preview;
    co_await resume_background();
    auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(request.save && !saveCancelled && !saveFailure) {
        try {
            if(destination) staging->Publish(*destination);
            else destination=staging->PublishUnique(request.directory,to_hstring(request.filename));
            saved=true;
        } catch(...) { saveFailure=std::current_exception(); }
    }
    // Saving, copying and preview are independent outputs from the same frame.
    if(request.copy || request.preview) {
        try {
            check_hresult(initialized);
            auto decoded=iima::DecodeScreenshot(staging->Path());
            if(request.preview) {
                try { preview=iima::MakeScreenshotPreview(decoded); } catch(...) { previewFailed=true; }
            }
            if(request.copy) pixels=std::move(decoded);
        } catch(...) { if(request.copy) copyFailure=std::current_exception(); if(request.preview) previewFailed=true; }
    }
    if(SUCCEEDED(initialized)) CoUninitialize();
    try { co_await ui; } catch(...) { co_return; }
    if(closing_) { screenshotPostProcessing_=false; pickerOpen_=false; FinishClose(); co_return; } // A completed publication is not rolled back.
    if(pixels) {
        try {
            HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
            iima::CopyScreenshotToClipboard(hwnd,*pixels); copied=true;
        } catch(...) { copyFailure=std::current_exception(); }
    }
    if(closing_) { screenshotPostProcessing_=false; pickerOpen_=false; FinishClose(); co_return; }
    if(preview && preferences_.Boolean("screenshotShowPreview") && preferences_.Boolean("enableOSD")) {
        try { ShowScreenshotPreview(*preview,saved ? destination : std::nullopt); }
        catch(...) { previewFailed=true; HideScreenshotPreview(); }
    }
    auto detail=[](std::exception_ptr failure)->hstring {
        if(!failure) return {};
        try { std::rethrow_exception(failure); }
        catch(hresult_error const& error) { return error.message(); }
        catch(std::exception const& error) { return to_hstring(error.what()); }
        catch(...) { return L"未知错误"; }
    };
    hstring report;
    if(request.save) report=saved ? L"截图已保存。" : saveCancelled ? L"已取消保存文件。" : L"截图保存失败："+detail(saveFailure)+L"。";
    if(request.copy) report=report+(copied ? hstring{L"已复制到剪贴板。"} : L"复制失败："+detail(copyFailure)+L"。");
    if(previewFailed) report=report+L"预览不可用。";
    if(!closing_) ShowError(report);
    screenshotPostProcessing_=false; pickerOpen_=false;
    if(closing_) { FinishClose(); co_return; }
    UpdateScreenshotSettings();
    OpenPendingFiles(); FlushActivation();
}
void MainWindow::WireScreenshotSettings() {
    auto weak=get_weak();
    SaveScreenshotAsButton().Click([weak](auto const&,auto const&) { if(auto self=weak.get()) self->SaveScreenshot(false,true); });
    ChooseScreenshotDirectoryButton().Click([weak](auto const&,auto const&) { if(auto self=weak.get()) self->ChooseScreenshotDirectory(); });
    ClearScreenshotDirectoryButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get(); self && !self->closing_ && self->userDataLoaded_ && !self->pickerOpen_) {
            self->ApplyAdvancedSetting("screenShotFolder",std::string{}); self->UpdateScreenshotSettings();
        }
    });
    UpdateScreenshotSettings();
}
void MainWindow::UpdateScreenshotSettings() {
    auto folder=preferences_.String("screenShotFolder");
    ScreenshotDirectoryText().Text(folder.empty() ? L"未设置目录：捕获后选择保存位置。" : L"截图目录："+to_hstring(folder));
    const bool allowed=userDataLoaded_ && !closing_ && !pickerOpen_;
    ChooseScreenshotDirectoryButton().IsEnabled(allowed);
    ClearScreenshotDirectoryButton().IsEnabled(allowed && !folder.empty());
}
fire_and_forget MainWindow::ChooseScreenshotDirectory() {
    auto lifetime=get_strong();
    if(closing_ || !userDataLoaded_ || pickerOpen_) co_return;
    const auto previous=preferences_.String("screenShotFolder");
    pickerOpen_=true; UpdateScreenshotSettings();
    try {
        Windows::Storage::Pickers::FolderPicker picker;
        HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto folder=co_await picker.PickSingleFolderAsync();
        if(folder && !closing_) {
            std::filesystem::path path(folder.Path().c_str());
            apartment_context ui; std::exception_ptr failure;
            co_await resume_background();
            try { iima::ScreenshotStaging::ValidateDirectory(path); } catch(...) { failure=std::current_exception(); }
            try { co_await ui; } catch(...) { co_return; }
            if(failure) std::rethrow_exception(failure);
            if(!closing_ && preferences_.String("screenShotFolder")==previous)
                ApplyAdvancedSetting("screenShotFolder",to_string(path.native()));
        }
    } catch(hresult_error const& error) { if(!closing_) ShowError(error.message()); }
      catch(...) { if(!closing_) ShowError(L"无法设置截图目录。"); }
    pickerOpen_=false;
    if(!closing_) { UpdateScreenshotSettings(); OpenPendingFiles(); FlushActivation(); }
}
}
