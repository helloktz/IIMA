// IINA KeyRecordView.keyDown -> KeyRecordViewController.recordedKeyDownWith
// -> KeyCodeHelper.mpvKeyCode. Copyright (c) 2016 lhc. GPL-3.0.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/KeyboardInput.h"
#include <winrt/Windows.System.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireKeyRecorder() {
    auto weak = get_weak();
    StartKeyRecordingButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->pickerOpen_) {
            self->recordingBindingKey_ = true;
            self->KeyRecordingSurface().Text(L"等待一个按键组合…");
            self->KeyRecordingStatus().Text(L"只在此录入框聚焦时捕获；Esc取消，Tab离开。不会执行绑定。");
            if (!self->KeyRecordingSurface().Focus(FocusState::Programmatic)) {
                self->recordingBindingKey_ = false;
                self->KeyRecordingStatus().Text(L"录入框无法获得焦点，请手工填写按键名称。");
            }
        }
    });
    CancelKeyRecordingButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            self->recordingBindingKey_ = false;
            self->KeyRecordingStatus().Text(L"已取消录入，原按键字段不变。");
            self->BindingRowKey().Focus(FocusState::Programmatic);
        }
    });
    KeyRecordingSurface().LostFocus([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->recordingBindingKey_) {
            self->recordingBindingKey_ = false;
            if (!self->closing_) self->KeyRecordingStatus().Text(L"焦点已离开，录入取消。");
        }
    });
    KeyRecordingSurface().PreviewKeyDown([weak](auto const&, Input::KeyRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || self->closing_ || !self->recordingBindingKey_ || self->pickerOpen_) return;
        using Key = Windows::System::VirtualKey;
        if (args.Key() == Key::Escape || args.Key() == Key::Tab) {
            self->recordingBindingKey_ = false;
            self->KeyRecordingStatus().Text(L"录入取消；Esc或Tab绑定可手工填写，但Tab仍保留原生焦点导航。");
            // Tab remains native keyboard traversal. Escape must not close a
            // containing page or accidentally become a playback shortcut.
            if (args.Key() == Key::Escape) args.Handled(true);
            return;
        }
        auto status = args.KeyStatus();
        if (status.WasKeyDown) { args.Handled(true); return; }
        const auto key = static_cast<std::uint32_t>(args.Key());
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        // Never consume Windows window-management/system combinations. The OS
        // may intercept other reserved chords before they reach this control.
        if ((alt && (key == VK_F4 || key == VK_SPACE || key == VK_TAB)) ||
            (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) return;
        auto chord = iima::WindowsMpvKey(key, status.ScanCode, status.IsExtendedKey);
        if (!chord) {
            self->KeyRecordingStatus().Text(L"请按非修饰键；Windows系统键、AltGr、IME和死键组合不录入。");
            return;
        }
        // The same conversion is used for actual playback dispatch, including
        // current layout, keypad Enter and Shift printable-character rules.
        args.Handled(true);
        self->recordingBindingKey_ = false;
        self->KeyRecordingSurface().Text(to_hstring(*chord));
        self->BindingRowKey().Text(to_hstring(*chord));
        self->KeyRecordingStatus().Text(L"已填入按键字段；填写动作并追加/替换行后才进入草稿，尚未应用。");
        // Keep focus in the read-only TextBox until the user moves it, so the
        // release/repeat of this chord cannot trigger the player's shortcuts.
    });
}
}
