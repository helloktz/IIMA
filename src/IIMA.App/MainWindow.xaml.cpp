#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include "../IIMA.Core/BuildInfo.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
hstring Clock(std::optional<double> seconds)
{
    if (!seconds || !std::isfinite(*seconds) || *seconds < 0) return L"--:--";
    auto n = static_cast<long long>(*seconds);
    std::wostringstream text;
    if (n >= 3600) text << n / 3600 << L":" << std::setfill(L'0') << std::setw(2);
    text << (n / 60) % 60 << L":" << std::setfill(L'0') << std::setw(2) << n % 60;
    return hstring{text.str()};
}
}
namespace winrt::IIMA::implementation {
MainWindow::MainWindow()
{
    InitializeComponent();
    Title(L"IIMA " + to_hstring(iima::version()));
    auto weak = get_weak();
    auto dispatcher = DispatcherQueue();
    session_ = iima::PlayerSession::Create(
        [dispatcher](std::function<void()> work) {
            dispatcher.TryEnqueue([work = std::move(work)] { work(); });
        },
        [weak](iima::PlaybackSnapshot const& state) {
            if (auto self = weak.get(); self && !self->closing_) self->Apply(state);
        });
    std::weak_ptr<iima::PlayerSession> session = session_;
    presenter_ = std::make_unique<iima::VideoPresenter>(VideoPanel(), [session](unsigned w, unsigned h) {
        if (auto current = session.lock()) current->SetCompositionSize(w, h);
    });
    OpenButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->OpenFile(); });
    PauseButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->session_->TogglePause(); });
    FullscreenButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ToggleFullscreen(); });
    PositionSlider().ValueChanged([weak](auto const&, auto const& args) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_) self->session_->Seek(args.NewValue());
    });
    auto addKey = [this](Windows::System::VirtualKey key, Windows::System::VirtualKeyModifiers modifiers,
                        std::function<void()> action) {
        Input::KeyboardAccelerator accelerator;
        accelerator.Key(key); accelerator.Modifiers(modifiers);
        accelerator.Invoked([action](auto const&, auto const& args) { action(); args.Handled(true); });
        Root().KeyboardAccelerators().Append(accelerator);
    };
    addKey(Windows::System::VirtualKey::O, Windows::System::VirtualKeyModifiers::Control,
           [weak] { if (auto self = weak.get()) self->OpenFile(); });
    addKey(Windows::System::VirtualKey::Space, Windows::System::VirtualKeyModifiers::None,
           [weak] { if (auto self = weak.get(); self && self->PauseButton().IsEnabled()) self->session_->TogglePause(); });
    addKey(Windows::System::VirtualKey::F11, Windows::System::VirtualKeyModifiers::None,
           [weak] { if (auto self = weak.get()) self->ToggleFullscreen(); });
    addKey(Windows::System::VirtualKey::Escape, Windows::System::VirtualKeyModifiers::None,
           [weak] { if (auto self = weak.get(); self && self->fullscreen_) self->ToggleFullscreen(); });
    AppWindow().Closing([weak](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
        if (auto self = weak.get(); self && !self->closeComplete_) {
            args.Cancel(true);
            self->BeginClose();
        }
    });
    wchar_t executable[32768]{};
    auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) {
        ShowError(L"无法定位播放器运行目录。");
        return;
    }
    session_->Start((std::filesystem::path{executable}.parent_path() / L"libmpv-2.dll").wstring());
}
void MainWindow::ShowError(hstring const& message) { StatusText().Text(message); }
winrt::fire_and_forget MainWindow::OpenFile()
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto file = co_await picker.PickSingleFileAsync();
        if (file && !closing_) {
            if (!presenter_->IsBound()) {
                ShowError(L"暂未开启播放：当前 libmpv 的交换链地址没有安全的生命周期交接协议。视频呈现通过 Windows 验证前，不启动无画面播放。");
            } else {
                session_->Open(to_string(file.Path()));
            }
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    pickerOpen_ = false;
}
void MainWindow::Apply(iima::PlaybackSnapshot const& state)
{
    updating_ = true;
    bool loaded = state.media == iima::MediaState::Loaded || state.media == iima::MediaState::Ended;
    PauseButton().IsEnabled(loaded && presenter_->IsBound());
    PauseButton().Content(box_value(state.paused ? L"播放" : L"暂停"));
    auto seekable = loaded && state.duration && *state.duration > 0;
    PositionSlider().IsEnabled(seekable && presenter_->IsBound());
    PositionSlider().Maximum(seekable ? *state.duration : 1.0);
    PositionSlider().Value(state.position.value_or(0));
    TimeLabel().Text(Clock(state.position) + L" / " + Clock(state.duration));
    EmptyLabel().Visibility(loaded ? Visibility::Collapsed : Visibility::Visible);
    if (!state.error.empty()) ShowError(to_hstring(state.error));
    else if (state.session == iima::SessionState::Ready && !presenter_->IsBound())
        ShowError(L"播放内核已初始化，视频合成桥接尚待完成。可选择文件；暂不启动播放。");
    else if (state.media == iima::MediaState::Loading) StatusText().Text(L"正在打开…");
    else if (loaded) StatusText().Text(state.buffering ? L"正在缓冲…" : L"");
    updating_ = false;
}
void MainWindow::ToggleFullscreen()
{
    if (closing_) return;
    try {
        fullscreen_ = !fullscreen_;
        AppWindow().SetPresenter(fullscreen_ ? Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen
                                           : Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
        FullscreenButton().Content(box_value(fullscreen_ ? L"退出全屏" : L"全屏"));
    } catch (hresult_error const& error) { fullscreen_ = !fullscreen_; ShowError(error.message()); }
}
void MainWindow::BeginClose()
{
    if (closing_) return;
    closing_ = true;
    Root().IsEnabled(false);
    presenter_.reset(); // UI detach before event consumer stop / mpv destruction.
    auto weak = get_weak();
    session_->CloseAsync([weak] {
        if (auto self = weak.get()) { self->closeComplete_ = true; self->Close(); }
    });
}
}
