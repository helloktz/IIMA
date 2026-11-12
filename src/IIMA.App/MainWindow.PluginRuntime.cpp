// IINA JavascriptPluginInstance lifecycle and JavascriptAPICore, native adaptation.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/PluginInspection.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
winrt::hstring PluginDisplayText(std::string_view text) {
    // JS strings can contain lone surrogates. Display replacement characters
    // instead of throwing through a DispatcherQueue callback on malformed UTF-8.
    if (text.empty()) return {};
    auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) return L"[无法显示插件消息]";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size))
        return L"[无法显示插件消息]";
    return winrt::hstring(result);
}
}
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::StartLocalPlugin() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || pluginStarting_ || pluginStopPending_ || pluginRuntime_) co_return;
    pluginStarting_ = true; pickerOpen_ = true;
    StartPluginButton().IsEnabled(false);
    auto apartment = apartment_context();
    try {
        Windows::Storage::Pickers::FolderPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto folder = co_await picker.PickSingleFolderAsync();
        if (folder && !closing_) {
            auto path = std::filesystem::path(folder.Path().c_str());
            pluginInspectionStop_ = std::stop_source{};
            auto stop = pluginInspectionStop_.get_token();
            PluginRuntimeStatus().Text(L"正在读取插件入口…");
            co_await resume_background();
            std::optional<iima::PluginProgram> program;
            std::wstring failure;
            try { program = iima::LoadPluginProgram(path, stop); }
            catch (hresult_error const& error) { failure = error.message(); }
            catch (std::exception const& error) { failure = to_hstring(error.what()); }
            catch (...) { failure = L"无法读取插件。"; }
            try { co_await apartment; } catch (...) { co_return; }
            if (!closing_ && !failure.empty()) PluginRuntimeStatus().Text(failure);
            if (!closing_ && program) {
                Controls::ContentDialog consent;
                consent.XamlRoot(Root().XamlRoot());
                consent.Title(box_value(L"运行受信任的插件？"));
                std::wstring message = L"名称：" + std::wstring(to_hstring(program->manifest.name)) +
                    L"\n作者：" + std::wstring(to_hstring(program->manifest.authorName)) +
                    L"\n标识：" + std::wstring(to_hstring(program->manifest.identifier)) +
                    L"\n\n该插件可暂停、继续、停止、跳转或调整此窗口的播放速度，读取播放时间和状态。";
                pluginOsdAllowed_ = program->manifest.HasPermission(iima::PluginPermission::ShowOSD);
                if (pluginOsdAllowed_) message += L"它还申请在播放器画面上显示文字。";
                message += L"\n不提供网络、任意文件、进程、HTML 或完整 IINA 插件 API。"
                    L"只有部分插件可以使用。运行时不是操作系统安全沙箱，请只运行你信任的来源。"
                    L"\n此次仅运行已读入的入口文本，不安装、不自动启动；可随时停止。";
                consent.Content(box_value(message));
                consent.PrimaryButtonText(L"运行此插件"); consent.CloseButtonText(L"取消");
                consent.DefaultButton(Controls::ContentDialogButton::Close);
                auto approved = (co_await consent.ShowAsync()) == Controls::ContentDialogResult::Primary;
                if (approved && !closing_) {
                    auto generation = ++pluginGeneration_;
                    auto weak = get_weak(); auto queue = DispatcherQueue();
                    auto pending = std::make_shared<std::atomic_uint>(0);
                    auto post = [queue, pending](std::function<void()> action) {
                        if (pending->fetch_add(1) >= 128) {
                            pending->fetch_sub(1);
                            throw std::runtime_error("Plugin UI dispatch queue limit exceeded.");
                        }
                        bool accepted{};
                        try {
                            accepted = queue.TryEnqueue([pending, action = std::move(action)] {
                                struct Completed {
                                    std::shared_ptr<std::atomic_uint> count;
                                    ~Completed() { count->fetch_sub(1); }
                                } completed{pending};
                                action();
                            });
                        } catch (...) { pending->fetch_sub(1); throw; }
                        if (!accepted) {
                            pending->fetch_sub(1);
                            throw std::runtime_error("Plugin owner dispatcher is unavailable.");
                        }
                    };
                    iima::plugins::PluginRuntime::Callbacks callbacks;
                    callbacks.command = [weak, post, generation](iima::plugins::PluginCommand command) {
                        post([weak, generation, command = std::move(command)]() mutable {
                            if (auto self = weak.get(); self && !self->closing_ && self->pluginRuntime_ &&
                                self->pluginGeneration_ == generation) self->DispatchPluginCommand(std::move(command));
                        });
                    };
                    callbacks.log = [weak, post, generation](std::string level, std::string text) {
                        post([weak, generation, level = std::move(level), text = std::move(text)] {
                            if (auto self = weak.get(); self && !self->closing_ && self->pluginGeneration_ == generation) {
                                self->pluginLog_ += L"[" + std::wstring(PluginDisplayText(level)) + L"] " +
                                    std::wstring(PluginDisplayText(text)) + L"\n";
                                if (self->pluginLog_.size() > 16384) {
                                    auto cut = self->pluginLog_.find(L'\n', self->pluginLog_.size() - 16384);
                                    self->pluginLog_.erase(0, cut == std::wstring::npos ? self->pluginLog_.size() : cut + 1);
                                }
                                self->PluginRuntimeLog().Text(self->pluginLog_);
                            }
                        });
                    };
                    callbacks.state = [weak, queue, generation](bool running, std::string message) {
                        queue.TryEnqueue([weak, generation, running, message = std::move(message)] {
                            if (auto self = weak.get(); self && !self->closing_ && self->pluginGeneration_ == generation) {
                                if (!running) self->StopLocalPlugin();
                                self->PluginRuntimeStatus().Text(PluginDisplayText(message));
                            }
                        });
                    };
                    pluginLog_.clear(); PluginRuntimeLog().Text(L"");
                    pluginProjection_.Reset(session_->Snapshot(), musicMode_.IsMusic());
                    pluginRuntime_ = iima::plugins::PluginRuntime::Start(std::move(program->source),
                        program->manifest.name, pluginOsdAllowed_, std::move(callbacks),
                        iima::plugins::PlaybackStatusJson(session_->Snapshot()));
                    StopPluginButton().IsEnabled(true);
                    PluginRuntimeStatus().Text(L"正在启动受限插件…");
                    // No synthetic window-loaded/file-loaded event: this plugin was
                    // started after the window/media; consumers read initial status.
                }
            }
        }
    } catch (hresult_error const& error) { if (!closing_) PluginRuntimeStatus().Text(error.message()); }
      catch (std::exception const& error) { if (!closing_) PluginRuntimeStatus().Text(to_hstring(error.what())); }
      catch (...) { if (!closing_) PluginRuntimeStatus().Text(L"无法启动插件。"); }
    pluginStarting_ = false; pickerOpen_ = false;
    if (!closing_) StartPluginButton().IsEnabled(!pluginRuntime_ && !pluginStopPending_);
    FinishClose();
}
void MainWindow::StopLocalPlugin() {
    ++pluginGeneration_; // queued actions cannot affect a later plugin/window state
    if (!pluginRuntime_) return;
    pluginRuntime_->Stop();
    pluginStopPending_ = true;
    if (!closing_) { StopPluginButton().IsEnabled(false); StartPluginButton().IsEnabled(false); PluginRuntimeStatus().Text(L"正在停止插件…"); }
    RetirePlugin(std::move(pluginRuntime_));
}
winrt::fire_and_forget MainWindow::RetirePlugin(std::unique_ptr<iima::plugins::PluginRuntime> runtime) {
    auto lifetime = get_strong();
    auto apartment = apartment_context();
    co_await resume_background();
    runtime.reset(); // joins a worker which never synchronously waits for UI
    try { co_await apartment; } catch (...) { co_return; }
    pluginStopPending_ = false;
    if (!closing_) {
        StartPluginButton().IsEnabled(!pluginStarting_);
        // Keep the final error/state report, do not replace it with false success.
        if (PluginRuntimeStatus().Text() == L"正在停止插件…") PluginRuntimeStatus().Text(L"插件已停止。");
    }
    FinishClose();
}
void MainWindow::UpdatePlugin(iima::PlaybackSnapshot const& state) {
    if (!pluginRuntime_ || closing_) return;
    try {
        auto snapshot = iima::plugins::PlaybackStatusJson(state);
        auto events = pluginProjection_.Update(state, musicMode_.IsMusic());
        bool accepted = true;
        if (events.empty()) accepted = pluginRuntime_->Update(std::move(snapshot));
        else for (auto const& event : events) {
            if (!pluginRuntime_->Update(snapshot, event.name, event.argumentJson)) { accepted = false; break; }
        }
        if (!accepted) {
            StopLocalPlugin();
            PluginRuntimeStatus().Text(L"插件事件队列已满或运行时已结束；已停止，未继续遗漏事件。");
        }
    } catch (...) {
        PluginRuntimeStatus().Text(L"无法投影播放状态，插件已停止。"); StopLocalPlugin();
    }
}
void MainWindow::DispatchPluginCommand(iima::plugins::PluginCommand command) {
    if (closing_ || !pluginRuntime_) return;
    // Exactly the same typed session operations as local controls. Acceptance is
    // asynchronous, not evidence that mpv has completed or accepted a command.
    if (command.name == "pause") session_->Pause(true);
    else if (command.name == "resume") session_->Pause(false);
    else if (command.name == "stop") session_->Stop();
    else if (command.name == "seek" && std::isfinite(command.number) && session_->Snapshot().seekable)
        session_->SeekRelative(command.number, command.flag);
    else if (command.name == "seekTo" && std::isfinite(command.number) && session_->Snapshot().seekable)
        session_->Seek(command.number);
    else if (command.name == "setSpeed" && std::isfinite(command.number)) session_->SetSpeed(command.number);
    else if (command.name == "osd" && pluginOsdAllowed_ && command.text.size() <= 4096 &&
        preferences_.Boolean("enableOSD")) {
        OSDTitle().Text(PluginDisplayText(command.text)); OSDDetail().Text(L"来自本地插件");
        OSDProgress().Visibility(Visibility::Collapsed); OSDBorder().Visibility(Visibility::Visible);
        osdTimer_.Stop(); osdTimer_.Interval(std::chrono::seconds(3)); osdTimer_.Start();
    }
}
}
