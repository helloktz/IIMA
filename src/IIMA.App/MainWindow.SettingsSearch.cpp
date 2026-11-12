// IINA SettingsSearch.swift / SettingsWindow search result navigation.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.System.h>
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
std::string FoldSettingsText(hstring const& input) {
    if (input.empty()) return {};
    const auto size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,input.c_str(),
        static_cast<int>(input.size()),nullptr,0,nullptr,nullptr,0);
    if (size<=0) return to_string(input); // Core still provides ASCII folding.
    std::wstring output(static_cast<std::size_t>(size),L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,input.c_str(),
        static_cast<int>(input.size()),output.data(),size,nullptr,nullptr,0)) return to_string(input);
    return to_string(output);
}
bool FocusEditor(DependencyObject const& node) {
    if (auto control=node.try_as<Controls::Control>(); control && control.IsEnabled() && control.IsTabStop())
        return control.Focus(FocusState::Programmatic);
    const auto count=Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i=0;i<count;++i) if (FocusEditor(Media::VisualTreeHelper::GetChild(node,i))) return true;
    return false;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireSettingsSearch() {
    auto weak=get_weak();
    auto changed=[weak](auto const&,auto const&) {
        if (auto self=weak.get(); self && !self->closing_) self->FilterAdvancedSettings();
    };
    AdvancedSettingsSearchBox().TextChanged(changed);
    AdvancedSettingsCategory().SelectionChanged(changed);
    AdvancedSettingsModifiedOnly().Checked(changed);
    AdvancedSettingsModifiedOnly().Unchecked(changed);
    ClearAdvancedSettingsSearchButton().Click([weak](auto const&,auto const&) {
        if (auto self=weak.get(); self && !self->closing_) {
            self->AdvancedSettingsSearchBox().Text(L"");
            self->AdvancedSettingsCategory().SelectedIndex(0);
            self->AdvancedSettingsModifiedOnly().IsChecked(false);
            self->AdvancedSettingsSearchBox().Focus(FocusState::Programmatic);
        }
    });
    AdvancedSettingsSearchBox().KeyDown([weak](auto const&,Input::KeyRoutedEventArgs const& args) {
        auto self=weak.get(); if (!self || self->closing_) return;
        if (args.Key()==Windows::System::VirtualKey::Escape) {
            self->AdvancedSettingsSearchBox().Text(L""); args.Handled(true);
        } else if (args.Key()==Windows::System::VirtualKey::Enter) {
            for (auto const& row:self->advancedSettingsRows_) {
                if (row.Visibility()!=Visibility::Visible) continue;
                row.StartBringIntoView(); FocusEditor(row); break;
            }
            args.Handled(true);
        }
    });
}
void MainWindow::FilterAdvancedSettings() {
    if (closing_) return;
    std::optional<iima::settings::PreferenceArea> category;
    const auto index=AdvancedSettingsCategory().SelectedIndex();
    if (index>=1 && index<=6) category=static_cast<iima::settings::PreferenceArea>(index-1);
    auto checked=AdvancedSettingsModifiedOnly().IsChecked();
    auto matches=iima::settings::SearchSettings(advancedSettingsIndex_,
        FoldSettingsText(AdvancedSettingsSearchBox().Text()),category,preferences_.Overrides(),checked && checked.Value());
    auto root=Root().XamlRoot();
    DependencyObject focused=root ? Input::FocusManager::GetFocusedElement(root).try_as<DependencyObject>() : nullptr;
    bool hidingFocus{};
    for (std::size_t i=0;i<advancedSettingsRows_.size();++i) {
        const bool visible=std::binary_search(matches.indices.begin(),matches.indices.end(),i);
        if (!visible) for (auto node=focused;node;node=Media::VisualTreeHelper::GetParent(node))
            if (node==advancedSettingsRows_[i]) { hidingFocus=true; break; }
        advancedSettingsRows_[i].Visibility(visible?Visibility::Visible:Visibility::Collapsed);
    }
    AdvancedSettingsSearchStatus().Text(matches.queryTooLarge ? L"搜索词过长或超过32个词，请缩短。" :
        L"显示 "+to_hstring(matches.indices.size())+L" / "+to_hstring(advancedSettingsRows_.size())+
        L" 项。匹配键名、mpv属性、分类和选项值；不会修改设置。Enter定位首项，Esc清空搜索词。");
    if (hidingFocus) AdvancedSettingsSearchBox().Focus(FocusState::Programmatic);
}
}
