// IINA SettingsPagePlugin / PluginViewController adapted to native inspection.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/PluginInspection.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.Pickers.h>

namespace winrt::IIMA::implementation {
void MainWindow::WirePluginInspection() {
    InspectPluginButton().Click([weak = get_weak()](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->InspectPlugin();
    });
}
winrt::fire_and_forget MainWindow::InspectPlugin() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || pluginInspectionBusy_) co_return;
    pluginInspectionBusy_ = true; pickerOpen_ = true;
    InspectPluginButton().IsEnabled(false);
    PluginInspectionText().Text(L"请选择包含 Info.json 的插件目录。不会执行脚本。");
    auto apartment = winrt::apartment_context();
    std::wstring report;
    try {
        Windows::Storage::Pickers::FolderPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto folder = co_await picker.PickSingleFolderAsync();
        pickerOpen_ = false;
        if (!folder || closing_) {
            if (!closing_) { InspectPluginButton().IsEnabled(true); PluginInspectionText().Text(L"检查已取消。"); }
            pluginInspectionBusy_ = false; co_return;
        }
        auto path = std::filesystem::path(folder.Path().c_str());
        pluginInspectionStop_ = std::stop_source{};
        auto stop = pluginInspectionStop_.get_token();
        PluginInspectionText().Text(L"正在读取插件清单…");
        co_await winrt::resume_background();
        try { report = iima::InspectPluginFolder(path, stop); }
        catch (hresult_error const& e) { report = L"插件检查失败：" + std::wstring(e.message()); }
        catch (std::exception const& e) { report = L"插件检查失败：" + std::wstring(to_hstring(e.what())); }
        catch (...) { report = L"插件检查失败。"; }
        co_await apartment;
    } catch (hresult_error const& e) { report = L"无法选择插件目录：" + std::wstring(e.message()); }
      catch (...) { report = L"无法检查插件目录。"; }
    pickerOpen_ = false; pluginInspectionBusy_ = false;
    if (!closing_) {
        PluginInspectionText().Text(report);
        InspectPluginButton().IsEnabled(true);
    }
}
}
