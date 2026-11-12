// IINA PrefOSCToolbarSettingsSheetController / OSCToolbarButton.
// Copyright © 2018 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace {
using Action=iima::settings::ToolbarAction;
hstring ActionLabel(Action action) {
    switch(action) {
    case Action::Settings:return L"设置";case Action::Playlist:return L"播放列表";
    case Action::Pip:return L"画中画";case Action::Fullscreen:return L"全屏";
    case Action::MusicMode:return L"音乐模式";case Action::SubtitleTrack:return L"字幕轨道";
    case Action::Screenshot:return L"截图";case Action::Plugins:return L"插件管理";
    }
    return L"未知动作";
}
void ShowToolbarTarget(Control const& target) {
    for(DependencyObject parent=target;parent;parent=Media::VisualTreeHelper::GetParent(parent))
        if(auto expander=parent.try_as<Expander>()) expander.IsExpanded(true);
    auto weak=make_weak(target);
    target.DispatcherQueue().TryEnqueue([weak] {
        if(auto current=weak.get();current && current.IsLoaded() && current.IsEnabled()) {
            for(DependencyObject node=current;node;node=Media::VisualTreeHelper::GetParent(node))
                if(auto element=node.try_as<UIElement>();element && element.Visibility()!=Visibility::Visible) return;
            current.StartBringIntoView();current.Focus(FocusState::Programmatic);
        }
    });
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireToolbarSettings() {
    auto weak=get_weak();
    auto edit=[weak](int operation) {if(auto self=weak.get()) self->EditToolbarDraft(operation);};
    ToolbarAddButton().Click([edit](auto const&,auto const&){edit(0);});
    ToolbarRemoveButton().Click([edit](auto const&,auto const&){edit(1);});
    ToolbarUpButton().Click([edit](auto const&,auto const&){edit(2);});
    ToolbarDownButton().Click([edit](auto const&,auto const&){edit(3);});
    ToolbarDefaultButton().Click([edit](auto const&,auto const&){edit(4);});
    ToolbarApplyButton().Click([weak](auto const&,auto const&){if(auto self=weak.get()) self->ApplyToolbarDraft();});
    ToolbarCancelButton().Click([weak](auto const&,auto const&){if(auto self=weak.get()) self->CancelToolbarDraft();});
    auto selection=[weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->toolbarProjecting_) self->UpdateToolbarDraftControls();
    };
    ToolbarCurrentList().SelectionChanged(selection);ToolbarAvailableBox().SelectionChanged(selection);
}
void MainWindow::CancelToolbarDraft() {
    if(closing_ || !userDataLoaded_) return;
    toolbarBaseline_=preferences_.String("controlBarToolbarButtons");
    auto parsed=iima::settings::ParseToolbarActions(toolbarBaseline_);
    if(!parsed) {ToolbarDraftStatus().Text(L"已保存的工具栏配置无效，未覆盖。");return;}
    toolbarDraft_=std::move(*parsed);toolbarDraftReady_=true;toolbarDraftDirty_=false;
    RenderToolbarDraft();
}
void MainWindow::RenderToolbarDraft(int selection) {
    toolbarProjecting_=true;
    ToolbarCurrentList().Items().Clear();ToolbarAvailableBox().Items().Clear();
    for(auto action:toolbarDraft_) ToolbarCurrentList().Items().Append(box_value(ActionLabel(action)));
    for(auto action:iima::settings::AvailableToolbarActions()) {
        if(std::find(toolbarDraft_.begin(),toolbarDraft_.end(),action)!=toolbarDraft_.end()) continue;
        ComboBoxItem item;item.Content(box_value(ActionLabel(action)));item.Tag(box_value(static_cast<int>(action)));
        ToolbarAvailableBox().Items().Append(item);
    }
    if(selection>=0 && static_cast<std::size_t>(selection)<toolbarDraft_.size()) ToolbarCurrentList().SelectedIndex(selection);
    if(ToolbarAvailableBox().Items().Size()) ToolbarAvailableBox().SelectedIndex(0);
    toolbarProjecting_=false;UpdateToolbarDraftControls();
}
void MainWindow::UpdateToolbarDraftControls() {
    if(closing_) return;
    bool enabled=userDataLoaded_ && toolbarDraftReady_;
    auto row=ToolbarCurrentList().SelectedIndex();
    bool selected=row>=0 && static_cast<std::size_t>(row)<toolbarDraft_.size();
    ToolbarAddButton().IsEnabled(enabled && toolbarDraft_.size()<iima::settings::MaxToolbarActions && ToolbarAvailableBox().SelectedIndex()>=0);
    ToolbarRemoveButton().IsEnabled(enabled && selected);
    ToolbarUpButton().IsEnabled(enabled && selected && row>0);
    ToolbarDownButton().IsEnabled(enabled && selected && static_cast<std::size_t>(row+1)<toolbarDraft_.size());
    ToolbarDefaultButton().IsEnabled(enabled);ToolbarApplyButton().IsEnabled(enabled && toolbarDraftDirty_);
    ToolbarCancelButton().IsEnabled(enabled && toolbarDraftDirty_);
    if(enabled && toolbarDraftDirty_ && preferences_.String("controlBarToolbarButtons")!=toolbarBaseline_)
        ToolbarDraftStatus().Text(L"工具栏偏好已在别处改变。请取消草稿，重新编辑，避免覆盖新配置。");
    else ToolbarDraftStatus().Text(toolbarDraftDirty_ ? L"草稿尚未应用。恢复默认也需点击应用。" : L"当前工具栏配置；最多5项。设置按钮打开高级偏好，插件按钮打开本地插件管理。");
}
void MainWindow::EditToolbarDraft(int operation) {
    if(closing_ || !userDataLoaded_ || !toolbarDraftReady_ || toolbarProjecting_) return;
    int row=ToolbarCurrentList().SelectedIndex();
    if(operation==0) {
        if(toolbarDraft_.size()>=iima::settings::MaxToolbarActions) return;
        auto item=ToolbarAvailableBox().SelectedItem().try_as<ComboBoxItem>();if(!item) return;
        auto action=static_cast<Action>(unbox_value<int>(item.Tag()));
        if(std::find(toolbarDraft_.begin(),toolbarDraft_.end(),action)!=toolbarDraft_.end()) return;
        toolbarDraft_.push_back(action);row=static_cast<int>(toolbarDraft_.size()-1);
    } else if(operation==4) {
        auto defaults=iima::settings::DefaultToolbarActions();toolbarDraft_.assign(defaults.begin(),defaults.end());row=-1;
    } else {
        if(row<0 || static_cast<std::size_t>(row)>=toolbarDraft_.size()) return;
        if(operation==1) {toolbarDraft_.erase(toolbarDraft_.begin()+row);row=std::min(row,static_cast<int>(toolbarDraft_.size())-1);}
        else if(operation==2 && row>0) {std::swap(toolbarDraft_[row],toolbarDraft_[row-1]);--row;}
        else if(operation==3 && static_cast<std::size_t>(row+1)<toolbarDraft_.size()) {std::swap(toolbarDraft_[row],toolbarDraft_[row+1]);++row;}
        else return;
    }
    toolbarDraftDirty_=true;RenderToolbarDraft(row);
}
void MainWindow::ApplyToolbarDraft() {
    if(closing_ || !userDataLoaded_ || !toolbarDraftReady_ || !toolbarDraftDirty_) return;
    if(preferences_.String("controlBarToolbarButtons")!=toolbarBaseline_) {UpdateToolbarDraftControls();return;}
    auto value=iima::settings::SerializeToolbarActions(toolbarDraft_);
    if(!iima::settings::ParseToolbarActions(value)) {ShowError(L"工具栏草稿无效，未应用。");return;}
    ApplyAdvancedSetting("controlBarToolbarButtons",value);
    if(preferences_.String("controlBarToolbarButtons")==value) {
        CancelToolbarDraft();ToolbarDraftStatus().Text(L"已应用到当前窗口；磁盘保存结果遵循设置保存状态。");
    }
}
bool MainWindow::ToolbarActionAvailable(Action action) {
    if(closing_ || !userDataLoaded_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_) return false;
    auto const& state=session_->Snapshot();
    using Command=iima::ApplicationCommand;
    switch(action) {
    case Action::Settings:case Action::Playlist:case Action::SubtitleTrack:case Action::Plugins:
        // Keep music-mode visibility preferences intact, rather than toggling
        // them merely to make a toolbar navigation button appear to succeed.
        return PlayerSidebar().Visibility()==Visibility::Visible;
    case Action::Pip:return commandRouter_.CanExecute(Command::TogglePictureInPicture);
    case Action::MusicMode:return commandRouter_.CanExecute(Command::ToggleMusicMode);
    case Action::Fullscreen:return true;
    case Action::Screenshot:return state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded &&
        !screenshotPostProcessing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
        (preferences_.Boolean("screenshotSaveToFile") || preferences_.Boolean("screenshotCopyToClipboard")) &&
        std::any_of(state.tracks.begin(),state.tracks.end(),[](auto const& track){return track.type=="video" && track.selected;});
    }
    return false;
}
void MainWindow::ExecuteToolbarAction(Action action) {
    if(!ToolbarActionAvailable(action)) return;
    ControlBarActivity();
    switch(action) {
    case Action::Settings:ShowToolbarTarget(AdvancedSettingsSearchBox());break;
    case Action::Playlist:ShowToolbarTarget(PlaylistView());break;
    case Action::SubtitleTrack:ShowToolbarTarget(SubtitleBox());break;
    case Action::Plugins:ShowToolbarTarget(InspectPluginButton());break;
    case Action::Pip:ExecuteApplicationCommand(iima::ApplicationCommand::TogglePictureInPicture);break;
    case Action::MusicMode:ExecuteApplicationCommand(iima::ApplicationCommand::ToggleMusicMode);break;
    case Action::Fullscreen:ToggleFullscreen();break;
    case Action::Screenshot:SaveScreenshot();break;
    }
}
void MainWindow::UpdateControlBarToolbar() {
    if(closing_ || !userDataLoaded_) return;
    auto value=preferences_.String("controlBarToolbarButtons");
    if(!toolbarBuilt_ || value!=toolbarApplied_) {
        auto actions=iima::settings::ParseToolbarActions(value);if(!actions) return;
        // Reparent the existing named FullscreenButton, preserving its original
        // click subscription and every mode-specific content update.
        auto fullscreen=FullscreenButton();
        OscToolbarItems().Children().Clear();toolbarButtons_.clear();
        fullscreen.Visibility(Visibility::Collapsed);
        auto weak=get_weak();
        for(auto action:*actions) {
            Button button=action==Action::Fullscreen ? fullscreen : Button{};
            if(action!=Action::Fullscreen) {
                button.Content(box_value(ActionLabel(action)));
                button.Click([weak,action](auto const&,auto const&) {if(auto self=weak.get()) self->ExecuteToolbarAction(action);});
            }
            button.Visibility(Visibility::Visible);OscToolbarItems().Children().Append(button);
            toolbarButtons_.emplace_back(action,button);
        }
        // Retain the named element in the tree even when not configured, so
        // all existing name lookups remain valid without adding visible space.
        if(std::find(actions->begin(),actions->end(),Action::Fullscreen)==actions->end())
            OscToolbarItems().Children().Append(fullscreen);
        toolbarBuilt_=true;toolbarApplied_=value;
    }
    for(auto const& [action,button]:toolbarButtons_) {
        button.IsEnabled(ToolbarActionAvailable(action));
        if(action==Action::MusicMode) button.Content(box_value(musicMode_.IsMusic() ? L"退出音乐模式" : L"音乐模式"));
    }
    if(!toolbarDraftReady_ || (!toolbarDraftDirty_ && toolbarBaseline_!=value)) CancelToolbarDraft();
    else UpdateToolbarDraftControls();
}
}
