// Port of PlayerWindowController.handleKeyBinding/keyDown and PrefKeyBindingViewController.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/KeyboardInput.h"
#include "../IIMA.Core/Input/BindingConfiguration.h"
#include "Platform/AtomicTextFile.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <charconv>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
constexpr std::string_view DefaultBindings = R"conf(# IIMA Windows defaults. Imported configurations replace these bindings.
SPACE cycle pause
LEFT seek -5 relative+exact
RIGHT seek 5 relative+exact
Ctrl+LEFT playlist-prev
Ctrl+RIGHT playlist-next
#@iina Ctrl+o open-file
#@iina Ctrl+l playlist-panel
Ctrl+Shift+s screenshot
F11 cycle fullscreen
ESC set fullscreen no
m cycle mute
[ add speed -0.1
] add speed 0.1
. frame-step
, frame-back-step
l ab-loop
)conf";
bool NativeControlOwnsKey(DependencyObject const& focused, Windows::System::VirtualKey key) {
    using Key = Windows::System::VirtualKey;
    // Tab is always reserved for keyboard traversal, even when a config binds it.
    if (key == Key::Tab) return true;
    // Explicit Ctrl/Alt player shortcuts remain available outside text entry.
    const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000);
    if (modified) return false;
    const bool navigation = key == Key::Left || key == Key::Right || key == Key::Up || key == Key::Down ||
        key == Key::Home || key == Key::End || key == Key::PageUp || key == Key::PageDown;
    const bool activate = key == Key::Space || key == Key::Enter;
    for (auto element = focused; element; element = Media::VisualTreeHelper::GetParent(element)) {
        if (auto combo = element.try_as<Controls::ComboBox>(); combo && combo.IsDropDownOpen())
            return true;
        if ((navigation || activate) && (element.try_as<Controls::Primitives::Selector>() ||
            element.try_as<Controls::Slider>() || element.try_as<Controls::TreeView>())) return true;
        if (activate && (element.try_as<Controls::Primitives::ButtonBase>() ||
            element.try_as<Controls::ToggleSwitch>())) return true;
        if (navigation && element.try_as<Controls::RadioButton>()) return true;
    }
    return false;
}
std::optional<double> Number(std::string const& text) {
    double value{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return {};
    return value;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::ApplyKeyboardConfiguration() {
    auto const text = userData_.settings.inputConf.value_or(std::string(DefaultBindings));
    auto parsed = iima::ParseInputConf(text);
    keyBindings_ = std::make_unique<iima::KeyBindingSet>(parsed.mappings);
    UpdatePluginKeyBindings();
    KeyBindingsEditor().Text(to_hstring(text));
    KeyBindingsStatus().Text(parsed.issues.empty() ? L"快捷键配置已载入。" : L"配置包含无法解析的行；有效条目已载入，请检查后重新保存。");
}
void MainWindow::WireKeyboardControls() {
    auto weak = get_weak();
    // Tunnelling event replaces hard-coded accelerators, so edited bindings can
    // actually replace defaults. Text entry and modal pickers retain all keys.
    Root().PreviewKeyDown([weak](auto const&, Input::KeyRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || self->closing_ || self->pickerOpen_ || !self->keyBindings_ ||
            !self->KeyboardPlaybackAllowed() || args.Handled()) return;
        auto focused = Input::FocusManager::GetFocusedElement(self->Root().XamlRoot()).try_as<DependencyObject>();
        if (NativeControlOwnsKey(focused, args.Key())) return;
        auto status = args.KeyStatus();
        auto key = iima::WindowsMpvKey(static_cast<std::uint32_t>(args.Key()), status.ScanCode, status.IsExtendedKey);
        if (!key) return;
        auto binding = self->keyBindings_->Find(*key);
        if (!binding) {
            if (auto saved = self->savedFilterLibrary_.FindShortcut(*key)) {
                args.Handled(true);
                if (!status.WasKeyDown) self->ApplySavedFilter(saved->first, saved->second);
            }
        } else if (status.WasKeyDown && (binding->IsIinaCommand() || binding->RawAction() == "quit" ||
            binding->RawAction().starts_with("screenshot"))) {
            args.Handled(true); // Repeat may be observed but does not reopen dialogs.
        } else if (self->DispatchKeyBinding(*binding)) args.Handled(true);
        // Low-priority observation follows the application's native action. It
        // cannot consume a routed event or suppress native/default behaviour.
        self->PublishPluginKey(*key, true, status.WasKeyDown);
    });
    Root().PreviewKeyUp([weak](auto const&, Input::KeyRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || self->closing_ || self->pickerOpen_ || args.Handled() ||
            !self->KeyboardPlaybackAllowed()) return;
        auto focused = Input::FocusManager::GetFocusedElement(self->Root().XamlRoot()).try_as<DependencyObject>();
        if (NativeControlOwnsKey(focused, args.Key())) return;
        auto status = args.KeyStatus();
        if (auto key = iima::WindowsMpvKey(static_cast<std::uint32_t>(args.Key()), status.ScanCode, status.IsExtendedKey))
            self->PublishPluginKey(*key, false, false);
    });
    SaveKeyBindingsButton().Click([weak](auto const&, auto const&) {
        auto self = weak.get();
        if (!self || self->closing_ || !self->userDataLoaded_) return;
        auto text = to_string(self->KeyBindingsEditor().Text());
        if (text.size() > 256 * 1024 || text.find('\0') != std::string::npos) {
            self->KeyBindingsStatus().Text(L"配置超过 256 KiB 或包含无效字符。"); return;
        }
        auto parsed = iima::ParseInputConf(text);
        if (!parsed.issues.empty()) {
            auto const& issue = parsed.issues.front();
            self->KeyBindingsStatus().Text(L"第 " + to_hstring(issue.line) + L" 行：" + to_hstring(issue.message)); return;
        }
        self->userData_.settings.inputConf = text;
        self->keyBindings_ = std::make_unique<iima::KeyBindingSet>(parsed.mappings);
        self->UpdatePluginKeyBindings();
        self->MarkUserDataDirty();
        self->KeyBindingsStatus().Text(L"已应用，正在保存设置。未移植的动作会在触发时明确提示。");
    });
    ResetKeyBindingsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_) {
            self->userData_.settings.inputConf.reset(); self->ApplyKeyboardConfiguration(); self->MarkUserDataDirty();
        }
    });
    ImportKeyBindingsButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ImportKeyBindings(); });
    ExportKeyBindingsButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ExportKeyBindings(); });
}
winrt::fire_and_forget MainWindow::ImportKeyBindings() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{}; check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L".conf"); picker.FileTypeFilter().Append(L".txt");
        auto file = co_await picker.PickSingleFileAsync();
        if (file && !closing_) {
            auto stream = co_await file.OpenReadAsync();
            if (stream.Size() > 256 * 1024) throw hresult_error(E_INVALIDARG, L"配置超过 256 KiB。");
            Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
            auto expected = static_cast<std::uint32_t>(stream.Size());
            auto count = co_await reader.LoadAsync(expected);
            if (count != expected) throw hresult_error(E_FAIL, L"配置文件读取不完整。");
            reader.UnicodeEncoding(Windows::Storage::Streams::UnicodeEncoding::Utf8);
            auto text = reader.ReadString(count);
            if (!closing_) {
                KeyBindingsEditor().Text(text);
                KeyBindingsStatus().Text(L"已导入编辑区，检查后点击“保存并应用”。");
            }
        }
    } catch (hresult_error const& error) { if (!closing_) KeyBindingsStatus().Text(error.message()); }
    pickerOpen_ = false;
}
winrt::fire_and_forget MainWindow::ExportKeyBindings() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    auto text = to_string(KeyBindingsEditor().Text());
    try {
        Windows::Storage::Pickers::FileSavePicker picker;
        HWND hwnd{}; check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.SuggestedFileName(L"input");
        picker.FileTypeChoices().Insert(L"mpv 快捷键配置", single_threaded_vector<hstring>({L".conf"}));
        auto file = co_await picker.PickSaveFileAsync();
        if (file && !closing_) {
            auto path = std::filesystem::path(file.Path().c_str());
            apartment_context ui;
            std::exception_ptr failure;
            co_await resume_background();
            try { iima::WriteUtf8Atomically(path, text); } catch (...) { failure = std::current_exception(); }
            co_await ui;
            if (failure) std::rethrow_exception(failure);
            if (!closing_) KeyBindingsStatus().Text(L"配置已导出。");
        }
    } catch (hresult_error const& error) { if (!closing_) KeyBindingsStatus().Text(error.message()); }
      catch (std::exception const& error) { if (!closing_) KeyBindingsStatus().Text(to_hstring(error.what())); }
    pickerOpen_ = false;
}
bool MainWindow::DispatchKeyBinding(iima::KeyMapping const& binding) {
    using Kind = iima::BindingActionKind;
    auto action = iima::ClassifyBindingAction(binding);
    switch (action.kind) {
    case Kind::Ignore: return true;
    case Kind::Application: ExecuteApplicationCommand(*action.application); return true;
    case Kind::AbLoop: session_->CycleABLoop(); return true;
    case Kind::Quit: BeginClose(); return true;
    case Kind::Screenshot: SaveScreenshot(); return true;
    case Kind::InactiveSection: return false;
    case Kind::MpvCommandTokens: break;
    default: ShowError(to_hstring(action.reason)); return true;
    }
    auto const& t = action.tokens;
    if (t.empty()) return false;
    auto const& command = t[0];
    auto unsupported = [this] { ShowError(L"该快捷键的命令或参数尚未移植，未执行。"); return true; };
    if (t.size() == 1) {
        if (command == "frame-step") session_->StepFrame();
        else if (command == "frame-back-step") session_->StepFrame(true);
        else if (command == "playlist-next") session_->PlaylistNext();
        else if (command == "playlist-prev") session_->PlaylistPrevious();
        else if (command == "stop") session_->Stop();
        else return unsupported();
        return true;
    }
    auto const state = session_->Snapshot();
    if (command == "seek" && (t.size() == 2 || t.size() == 3)) {
        auto amount = Number(t[1]);
        if (!amount) return unsupported();
        auto flags = t.size() == 3 ? t[2] : "relative";
        if (flags == "relative" || flags == "relative+exact" || flags == "relative+keyframes")
            session_->SeekRelative(*amount, flags == "relative+exact");
        else if (flags == "absolute" || flags == "absolute+exact") session_->Seek(*amount);
        else if ((flags == "absolute-percent" || flags == "absolute-percent+exact") && state.duration &&
            *amount >= 0 && *amount <= 100) session_->Seek(*state.duration * *amount / 100);
        else return unsupported();
        return true;
    }
    if ((command == "sub-step" || command == "sub-seek") && (t.size() == 2 || t.size() == 3)) {
        auto amount = Number(t[1]);
        if (!amount || (*amount != -1 && *amount != 0 && *amount != 1)) return unsupported();
        bool primary = true;
        if (t.size() == 3) {
            if (t[2] == "secondary") primary = false;
            else if (t[2] != "primary") return unsupported();
        }
        if (command == "sub-step") session_->StepSubtitleTiming(static_cast<int>(*amount), primary);
        else session_->SeekSubtitle(static_cast<int>(*amount), primary);
        return true;
    }
    if (command == "cycle" && t.size() == 2) {
        if (t[1] == "pause") session_->TogglePause();
        else if (t[1] == "mute") session_->ToggleMute();
        else if (t[1] == "sub-visibility") session_->SetSubtitlesVisible(!state.subtitlesVisible);
        else if (t[1] == "fullscreen") ToggleFullscreen();
        else if (t[1] == "video" || t[1] == "vid") session_->CycleTrack("video");
        else if (t[1] == "audio" || t[1] == "aid") session_->CycleTrack("audio");
        else if (t[1] == "sub" || t[1] == "sid") session_->CycleTrack("sub");
        else return unsupported();
        return true;
    }
    if (command == "set" && t.size() == 3 && (t[2] == "yes" || t[2] == "no")) {
        bool value = t[2] == "yes";
        if (t[1] == "pause") session_->Pause(value);
        else if (t[1] == "mute") session_->SetMuted(value);
        else if (t[1] == "sub-visibility") session_->SetSubtitlesVisible(value);
        else if (t[1] == "fullscreen") { if (fullscreen_ != value) ToggleFullscreen(); }
        else return unsupported();
        return true;
    }
    if ((command == "set" || command == "add") && t.size() == 3) {
        auto number = Number(t[2]);
        if (!number) return unsupported();
        if (t[1] == "chapter" && command == "add" && (*number == -1 || *number == 1))
            session_->StepChapter(static_cast<int>(*number));
        else if (t[1] == "volume") session_->SetVolume(*number + (command == "add" ? state.volume : 0));
        else if (t[1] == "speed") session_->SetSpeed(*number + (command == "add" ? state.speed : 0));
        else return unsupported();
        return true;
    }
    return unsupported();
}
}
