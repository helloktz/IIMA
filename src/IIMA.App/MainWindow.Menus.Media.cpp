// IINA MenuController.updateAudioDevice/updateSavedFiltersMenu and SavedFilter.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <set>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
Controls::MenuFlyoutItem MediaMenuItem(hstring const& text, bool enabled, bool checked = false) {
    Controls::MenuFlyoutItem item;
    item.Text(text); item.IsEnabled(enabled);
    // Read-only projection: a click never changes the check optimistically.
    if (checked) item.Icon(Controls::SymbolIcon{Controls::Symbol::Accept});
    return item;
}
void ShowMediaMenuTarget(Controls::Control const& target) {
    for (DependencyObject parent = target; parent; parent = Media::VisualTreeHelper::GetParent(parent))
        if (auto expander = parent.try_as<Controls::Expander>()) expander.IsExpanded(true);
    auto weak = make_weak(target);
    target.DispatcherQueue().TryEnqueue([weak] {
        if (auto current = weak.get(); current && current.IsLoaded()) {
            current.StartBringIntoView();
            current.Focus(FocusState::Programmatic);
        }
    });
}
bool SameSavedDefinition(iima::SavedFilter const& a, iima::SavedFilter const& b) {
    return a.name == b.name && a.filterString == b.filterString &&
        a.shortcutKey == b.shortcutKey && a.modifiers == b.modifiers;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::FillAudioDeviceMenu(Controls::MenuFlyoutSubItem const& menu) {
    // Existing static menu rows track popup lifetime. Never replace an open
    // popup's identities under the pointer; the caller keeps its dirty revision.
    if (!playerMenuVisibleItems_.empty()) return;
    menu.Items().Clear();
    auto const& state = session_->Snapshot();
    const bool enabled = !closing_ && userDataLoaded_ && !pickerOpen_ &&
        !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded;
    const auto generation = state.mediaGeneration, intent = state.mediaOpenIntentRevision;
    const auto revision = state.audioDevicesRevision;
    auto weak = get_weak();
    auto append = [&](std::string name, std::string description) {
        auto item = MediaMenuItem(to_hstring(description.empty() ? name : description), enabled && (name == "auto" || state.audioDevicesObserved),
            state.audioDeviceObserved && state.audioDevice == name);
        Controls::ToolTipService::SetToolTip(item, box_value(to_hstring(name)));
        item.Click([weak, generation, intent, revision, name, description](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ ||
                self->interactiveCrop_.Active() || self->cropOriginal_) return;
            auto const& current = self->session_->Snapshot();
            if (current.session != iima::SessionState::Ready || current.media != iima::MediaState::Loaded ||
                current.mediaGeneration != generation || current.mediaOpenIntentRevision != intent ||
                current.audioDevicesRevision != revision || self->session_->PlaylistEditBusy()) {
                self->ShowError(L"媒体或音频设备列表已变化，请重新打开设备菜单。"); return;
            }
            if (name != "auto" && (!current.audioDevicesObserved || std::none_of(current.audioDevices.begin(), current.audioDevices.end(),
                [&](auto const& device) { return device.name == name && device.description == description; }))) {
                self->ShowError(L"此音频设备已不可用，请重新打开设备菜单。"); return;
            }
            // Same typed preference path as the existing audio-device selector.
            // The subsequent mpv observation, not this accepted write, checks it.
            self->ApplyAdvancedSetting("audioDevice", name);
        });
        menu.Items().Append(item);
    };
    append("auto", "系统默认音频输出");
    std::set<std::string> names{"auto"};
    std::size_t shown{}, total{};
    for (auto const& device : state.audioDevices) {
        if (device.name.empty() || !names.insert(device.name).second) continue;
        ++total;
        if (shown == 99) continue;
        append(device.name, device.description);
        ++shown;
    }
    auto more = MediaMenuItem(total > shown ? L"仅显示前99个设备；打开音频设备设置查看全部" : L"音频设备设置…",
        !closing_ && userDataLoaded_ && !pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_);
    more.Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_ && !self->pickerOpen_ &&
            !self->interactiveCrop_.Active() && !self->cropOriginal_) {
            ShowMediaMenuTarget(self->AudioDeviceBox());
        }
    });
    menu.Items().Append(Controls::MenuFlyoutSeparator{});
    menu.Items().Append(more);
}
void MainWindow::FillSavedFilterMenu(Controls::MenuFlyoutSubItem const& menu, bool audio) {
    if (!playerMenuVisibleItems_.empty()) return;
    menu.Items().Clear();
    auto const& state = session_->Snapshot();
    auto const& chain = audio ? state.audioFilters : state.videoFilters;
    auto const& definitions = savedFilterLibrary_.Values(audio);
    const auto generation = state.mediaGeneration, intent = state.mediaOpenIntentRevision;
    const auto libraryRevision = savedFilterLibrary_.Revision(), chainRevision = chain.revision;
    const bool enabled = !closing_ && userDataLoaded_ && !pickerOpen_ &&
        !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded && chain.complete;
    auto weak = get_weak();
    const auto count = std::min<std::size_t>(100, definitions.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto definition = definitions[index];
        const auto observed = savedFilterLibrary_.ObservedState(audio, index, chain);
        auto label = to_hstring(definition.name);
        if (observed == iima::SavedFilterObservedState::Disabled) label = label + L"（当前停用）";
        else if (observed == iima::SavedFilterObservedState::Unavailable) label = label + L"（状态未知）";
        auto item = MediaMenuItem(label, enabled && observed != iima::SavedFilterObservedState::Unavailable,
            observed == iima::SavedFilterObservedState::Enabled);
        // Do not advertise a saved shortcut that input.conf or Windows may own.
        Controls::ToolTipService::SetToolTip(item, box_value(to_hstring(definition.filterString)));
        item.Click([weak, generation, intent, libraryRevision, chainRevision, audio, index, definition](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ ||
                self->interactiveCrop_.Active() || self->cropOriginal_) return;
            auto const& current = self->session_->Snapshot();
            auto const& currentChain = audio ? current.audioFilters : current.videoFilters;
            auto const& currentDefinitions = self->savedFilterLibrary_.Values(audio);
            if (current.session != iima::SessionState::Ready || current.media != iima::MediaState::Loaded ||
                current.mediaGeneration != generation || current.mediaOpenIntentRevision != intent ||
                self->session_->PlaylistEditBusy() || self->savedFilterLibrary_.Revision() != libraryRevision ||
                index >= currentDefinitions.size() || !SameSavedDefinition(currentDefinitions[index], definition) ||
                !currentChain.complete || currentChain.revision != chainRevision) {
                self->ShowError(L"媒体、保存的滤镜或当前滤镜链已变化，请重新打开菜单。"); return;
            }
            // PlanToggle uses parsed filter identity and existing guarded chain
            // editing. This route never submits an arbitrary mpv command string.
            self->ApplySavedFilter(audio, index);
        });
        menu.Items().Append(item);
    }
    if (!count) menu.Items().Append(MediaMenuItem(L"尚无保存的滤镜", false));
    auto manage = MediaMenuItem(definitions.size() > count ? L"仅显示前100项；管理保存的滤镜查看全部…" : L"管理保存的滤镜…",
        !closing_ && userDataLoaded_ && !pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_);
    manage.Click([weak, audio](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_ && !self->pickerOpen_ &&
            !self->interactiveCrop_.Active() && !self->cropOriginal_) {
            self->FilterTypeBox().SelectedIndex(audio ? 1 : 0);
            ShowMediaMenuTarget(self->SavedFilterView());
        }
    });
    menu.Items().Append(Controls::MenuFlyoutSeparator{});
    menu.Items().Append(manage);
}
}
