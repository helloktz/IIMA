// IINA SidebarAudioPane user profiles and EqualizerView, copyright lhc.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireEqualizerEditor() {
    auto weak = get_weak();
    auto frequencies = iima::Equalizations::Frequencies();
    for (std::size_t index = 0; index < frequencies.size(); ++index) {
        Controls::NumberBox box;
        box.Header(box_value(to_hstring(frequencies[index]) + L" Hz / dB"));
        box.Minimum(iima::EQProfile::SliderMinimum); box.Maximum(iima::EQProfile::SliderMaximum);
        box.SmallChange(0.5); box.LargeChange(3); box.Value(0);
        equalizerBands_[index] = box;
        EqualizerBandsPanel().Children().Append(box);
    }
    ApplyCustomEqualizerButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto profile = self->ReadEqualizerDraft();
            if (profile) self->session_->SetAudioEqualizer(*profile);
        }
    });
    LoadObservedEqualizerButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto const& chain = self->session_->Snapshot().audioFilters;
            std::optional<std::string> expression;
            for (auto const& entry : chain.entries)
                if (entry.filter && entry.filter->Label() && *entry.filter->Label() == iima::Equalizations::FilterLabel)
                    expression = entry.filter->StringFormat();
            auto profile = iima::Equalizations::SlidersFromFilter(expression ?
                std::optional<std::string_view>(*expression) : std::nullopt);
            self->SetEqualizerDraft(profile);
        }
    });
    LoadUserEqualizerButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto row = self->UserEqualizerBox().SelectedIndex();
            if (row < 0 || static_cast<std::size_t>(row) >= self->equalizerNames_.size()) return;
            auto found = self->userData_.settings.userEqualizers.find(self->equalizerNames_[row]);
            if (found != self->userData_.settings.userEqualizers.end()) {
                self->SetEqualizerDraft(found->second);
                self->UserEqualizerName().Text(to_hstring(found->first));
            }
        }
    });
    SaveUserEqualizerButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->EditUserEqualizer(0); });
    RenameUserEqualizerButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->EditUserEqualizer(1); });
    DeleteUserEqualizerButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->EditUserEqualizer(2); });
}
std::optional<iima::EQProfile> MainWindow::ReadEqualizerDraft() {
    iima::EQProfile profile;
    for (std::size_t index = 0; index < profile.gains.size(); ++index) profile.gains[index] = equalizerBands_[index].Value();
    if (!profile.IsFinite()) { ShowError(L"均衡器增益必须是有效数值。"); return {}; }
    return profile;
}
void MainWindow::SetEqualizerDraft(iima::EQProfile const& profile) {
    for (std::size_t index = 0; index < profile.gains.size(); ++index)
        equalizerBands_[index].Value(std::clamp(profile.gains[index], iima::EQProfile::SliderMinimum, iima::EQProfile::SliderMaximum));
}
void MainWindow::RefreshUserEqualizers() {
    equalizerNames_.clear(); UserEqualizerBox().Items().Clear();
    for (auto const& [name, profile] : userData_.settings.userEqualizers) {
        equalizerNames_.push_back(name);
        UserEqualizerBox().Items().Append(box_value(to_hstring(name)));
    }
}
void MainWindow::EditUserEqualizer(int operation) {
    if (closing_ || !userDataLoaded_) return;
    iima::UserEQProfiles library;
    if (!library.Restore(userData_.settings.userEqualizers)) { ShowError(L"均衡预设无法读取。"); return; }
    auto row = UserEqualizerBox().SelectedIndex();
    auto name = to_string(UserEqualizerName().Text());
    if (name.size() > 1024 || name.find('\0') != std::string::npos) { ShowError(L"预设名称过长或无效。"); return; }
    std::vector<std::string> builtinNames;
    for (auto const& item : iima::Equalizations::Presets()) builtinNames.emplace_back(item.key);
    iima::UserEQProfiles::EditResult result = iima::UserEQProfiles::EditResult::Applied;
    if (operation == 0) {
        auto profile = ReadEqualizerDraft(); if (!profile) return;
        if (library.Values().size() >= 512) { ShowError(L"最多保存512个均衡预设。"); return; }
        result = library.Add(name, *profile, builtinNames);
    } else {
        if (row < 0 || static_cast<std::size_t>(row) >= equalizerNames_.size()) return;
        auto selected = equalizerNames_[row];
        if (operation == 1) result = library.Rename(selected, name, builtinNames);
        else if (operation == 2) library.Remove(selected);
        else return;
    }
    if (result != iima::UserEQProfiles::EditResult::Applied) {
        ShowError(L"名称不能为空或与现有预设重复，增益必须有效。"); return;
    }
    userData_.settings.userEqualizers = library.Values();
    RefreshUserEqualizers(); MarkUserDataDirty();
    // Saving a preset changes no active audio. Explicit Apply uses observed af.
}
}
