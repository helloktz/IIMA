// IINA JavascriptPluginInstance lifecycle and JavascriptAPICore, native adaptation.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/PluginInspection.h"
#include "Platform/PluginPreferenceStore.h"
#include "Platform/PluginFileStore.h"
#include "Platform/PluginHttpClient.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <chrono>
#include <atomic>
#include <algorithm>
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
            std::string storedPreferences;
            try {
                program = iima::LoadPluginProgram(path, stop);
                storedPreferences = iima::PluginPreferenceStore::Load(program->manifest.identifier);
            }
            catch (hresult_error const& error) { failure = error.message(); }
            catch (std::exception const& error) { failure = to_hstring(error.what()); }
            catch (...) { failure = L"无法读取插件。"; }
            try { co_await apartment; } catch (...) { co_return; }
            if (!closing_ && !failure.empty()) PluginRuntimeStatus().Text(failure);
            if (!closing_ && program && failure.empty()) {
                Controls::ContentDialog consent;
                consent.XamlRoot(Root().XamlRoot());
                consent.Title(box_value(L"运行受信任的插件？"));
                std::wstring message = L"名称：" + std::wstring(to_hstring(program->manifest.name)) +
                    L"\n作者：" + std::wstring(to_hstring(program->manifest.authorName)) +
                    L"\n标识：" + std::wstring(to_hstring(program->manifest.identifier)) +
                    L"\n\n该插件可暂停、继续、停止、跳转或调整此窗口的播放速度，读取播放时间、状态、轨道和章节元数据，并可调整音量、静音、延迟、轨道和章节。";
                pluginOsdAllowed_ = program->manifest.HasPermission(iima::PluginPermission::ShowOSD);
                if (pluginOsdAllowed_) message += L"它还申请在播放器画面上显示文字。";
                message += L"\n它可读取当前播放列表的完整文件路径、网络地址（可能含令牌）和标题，并播放、移除或移动列表条目。请勿对含秘密地址的列表运行不受信任插件。\n它还可读取并保存此标识对应的插件偏好；相同标识的其他插件可读取这些值，请勿用于保存密码或令牌。它还可在此窗口的插件菜单中添加命令，读取当前快捷键配置的完整动作文本，并观察播放器区域按键（不含文本输入、密码输入和对话框）及视频区域鼠标操作。请勿把秘密写在快捷键动作中。"
                    L"\n它可读取、覆盖和删除此标识的私有 @data 文件，并读写本次运行的 @tmp 文件；相同标识可共享 @data，不应保存密码或令牌。它也可在两个私有目录内移动普通文件（不覆盖）；移入 @tmp 的文件会在停止插件时清理，移入 @data 则保留。只支持有界文本及普通文件，不开放你的其他文件。"
                    L"\n不提供任意文件、进程、HTML 或完整 IINA 插件 API。"
                    L"只有部分插件可以使用。运行时不是操作系统安全沙箱，请只运行你信任的来源。"
                    L"\n此次仅运行已读入的入口和目录内 JS 模块快照，不安装、不自动启动；可随时停止。";
                auto networkAllowed = program->manifest.HasPermission(iima::PluginPermission::NetworkRequest);
                if (networkAllowed) {
                    message += L"\n\n该插件还可通过 HTTPS 向以下声明的域名发送请求，包括 POST/PUT/PATCH/DELETE。它可以发送自身掌握的播放状态、轨道/章节、播放列表路径和网络地址、按键与私有插件数据；请只批准可信插件。服务端操作可能不可撤销。\n";
                    for (auto const& domain : program->manifest.allowedDomains)
                        message += L" • " + std::wstring(to_hstring(domain)) + L"\n";
                    if (program->manifest.allowedDomains.empty()) message += L"（未声明域名，所有请求将拒绝）\n";
                    message += L"它还可向许可域名发送 XML-RPC 调用，以及下载最多1 MiB的内容并覆盖私有 @data/@tmp 文件，不会自动打开或执行。\n不跟随重定向、不使用浏览器Cookie或系统登录；域名许可不是网络隔离，域名仍可能解析到私有地址。";
                } else message += L"\n该插件未申请网络权限，网络请求将拒绝。";
                Controls::TextBlock consentText;
                consentText.Text(message); consentText.TextWrapping(TextWrapping::Wrap);
                consentText.IsTextSelectionEnabled(true);
                Controls::ScrollViewer consentScroll;
                consentScroll.MaxHeight(420); consentScroll.Content(consentText);
                consent.Content(consentScroll);
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
                    auto files = std::make_shared<iima::PluginFileStore>(program->manifest.identifier);
                    if (networkAllowed) {
                        auto http = std::make_shared<iima::PluginHttpClient>(program->manifest, files);
                        callbacks.httpRequest = [http](std::string request, std::stop_token stop) {
                            return http->Request(request, stop);
                        };
                    }
                    callbacks.cleanup = [files] { files->CleanupTemporary(); };
                    callbacks.fileOperation = [files](std::string operation, std::string arguments) {
                        return files->Invoke(operation, arguments);
                    };
                    callbacks.menuChanged = [weak, post, generation](std::string json) {
                        post([weak, generation, json = std::move(json)] {
                            if (auto self = weak.get(); self && !self->closing_ && self->pluginRuntime_ &&
                                self->pluginGeneration_ == generation) self->ApplyPluginMenu(json);
                        });
                    };
                    callbacks.preferencesSync = [identifier = program->manifest.identifier,
                        base = storedPreferences](std::string edited) mutable {
                        // Called only by the JS worker: no UI wait, and no live
                        // module code participates in path selection or JSON I/O.
                        iima::PluginPreferenceStore::SaveMerged(identifier, base, edited);
                        base = std::move(edited); // only advance after atomic publish
                    };
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
                    pluginMediaEntry_ = session_->Snapshot().entryId;
                    pluginMediaState_ = session_->Snapshot().media;
                    ++pluginMediaEpoch_;
                    pluginProjection_.Reset(session_->Snapshot(), musicMode_.IsMusic());
                    pluginRuntime_ = iima::plugins::PluginRuntime::Start(std::move(program->source),
                        program->manifest.name, pluginOsdAllowed_, std::move(callbacks),
                        iima::plugins::PlaybackStatusJson(session_->Snapshot(), pluginMediaEpoch_),
                        program->manifest.entry, std::move(program->modules),
                        std::move(program->preferenceDefaultsJson), std::move(storedPreferences), PluginKeyBindingsJson());
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
    pluginPointer_.Reset();
    ++pluginGeneration_; // queued actions cannot affect a later plugin/window state
    ClearPluginMenu();
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
        if (pluginMediaEntry_ != state.entryId || (state.media == iima::MediaState::Loading &&
            pluginMediaState_ != iima::MediaState::Loading)) ++pluginMediaEpoch_;
        pluginMediaEntry_ = state.entryId; pluginMediaState_ = state.media;
        auto snapshot = iima::plugins::PlaybackStatusJson(state, pluginMediaEpoch_);
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
        StopLocalPlugin(); PluginRuntimeStatus().Text(L"无法投影播放状态，插件已停止。");
    }
}
void MainWindow::DispatchPluginCommand(iima::plugins::PluginCommand command) {
    if (closing_ || !pluginRuntime_) return;
    if (command.name.starts_with("playlist.")) { DispatchPluginPlaylistCommand(command); return; }
    if ((command.name == "playChapter" || command.name == "audio.id" ||
        command.name == "subtitle.id" || command.name == "video.id") &&
        command.mediaToken != std::to_string(pluginMediaEpoch_)) return;
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
    else if (command.name == "playChapter" && std::isfinite(command.number) && command.number >= 0 &&
        command.number < static_cast<double>(session_->Snapshot().chapters.size()))
        session_->SelectChapter(static_cast<std::size_t>(command.number));
    else if (command.name == "audio.volume" && std::isfinite(command.number)) session_->SetVolume(command.number);
    else if (command.name == "audio.muted") session_->SetMuted(command.flag);
    else if (command.name == "audio.delay" && std::isfinite(command.number)) session_->SetAudioDelay(command.number);
    else if (command.name == "subtitle.delay" && std::isfinite(command.number)) session_->SetSubtitleDelay(command.number);
    else if ((command.name == "audio.id" || command.name == "subtitle.id" || command.name == "video.id") &&
        std::isfinite(command.number) && command.number >= 0 && command.number <= 9007199254740991.0 &&
        std::floor(command.number) == command.number) {
        auto type = command.name == "audio.id" ? "audio" : command.name == "video.id" ? "video" : "sub";
        auto id = static_cast<std::int64_t>(command.number);
        // Track IDs belong to the current media. Never silently select a missing
        // track when the UI consumes a queued script command after a media switch.
        auto const& tracks = session_->Snapshot().tracks;
        if (id == 0 || std::any_of(tracks.begin(), tracks.end(), [&](auto const& track) {
            return track.type == type && track.id == id;
        })) session_->SelectTrack(type, id);
    }
    else if (command.name == "osd" && pluginOsdAllowed_ && command.text.size() <= 4096 &&
        preferences_.Boolean("enableOSD")) {
        OSDTitle().Text(PluginDisplayText(command.text)); OSDDetail().Text(L"来自本地插件");
        OSDProgress().Visibility(Visibility::Collapsed); OSDBorder().Visibility(Visibility::Visible);
        osdTimer_.Stop(); osdTimer_.Interval(std::chrono::seconds(3)); osdTimer_.Start();
    }
}
}
