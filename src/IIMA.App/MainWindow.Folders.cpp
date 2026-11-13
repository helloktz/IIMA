// IINA AppDelegate.openFile permits directory selection; Windows picker adapter.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>

namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::OpenFolder(bool append, bool invertWindowPolicy)
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    try {
        Windows::Storage::Pickers::FolderPicker picker;
        HWND hwnd{};
        check_hresult(GetNativeWindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto folder = co_await picker.PickSingleFolderAsync();
        if (folder && !closing_) {
            auto path = folder.Path();
            if (path.empty()) ShowError(L"请选择可访问的本地文件夹。");
            else QueueFiles({to_string(path)}, append, !append, invertWindowPolicy);
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
      catch (std::exception const& error) { if (!closing_) ShowError(to_hstring(error.what())); }
    pickerOpen_ = false;
    // Folder enumeration and target initialization remain asynchronous. Cancel
    // does not discard a prior OS activation waiting for this window's startup.
    if (!closing_) { OpenPendingFiles(); FlushActivation(); }
}
}
