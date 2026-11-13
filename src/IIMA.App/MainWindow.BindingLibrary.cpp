// IINA SettingsPageKeyBindings.ConfigEditor named-set workflow adapted to WinUI.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Input/BindingLibrary.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
std::optional<std::string> MainWindow::SelectedBindingSnapshot() {
    if (auto item = BindingSnapshotBox().SelectedItem().try_as<Controls::ComboBoxItem>())
        return to_string(unbox_value<hstring>(item.Tag()));
    return {};
}
void MainWindow::WireBindingLibrary() {
    auto weak = get_weak();
    SaveBindingSnapshotButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SaveBindingSnapshot(false);
    });
    UpdateBindingSnapshotButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SaveBindingSnapshot(true);
    });
    RenameBindingSnapshotButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RenameBindingSnapshot();
    });
    DeleteBindingSnapshotButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->DeleteBindingSnapshot();
    });
    UndoBindingSnapshotButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->UndoBindingSnapshot();
    });
    LoadBindingSnapshotButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->LoadBindingSnapshot();
    });
    BindingSnapshotBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->projectingBindingLibrary_) {
            const bool selected = self->SelectedBindingSnapshot().has_value();
            self->LoadBindingSnapshotButton().IsEnabled(selected);
            self->UpdateBindingSnapshotButton().IsEnabled(selected);
            self->RenameBindingSnapshotButton().IsEnabled(selected);
            self->DeleteBindingSnapshotButton().IsEnabled(selected);
        }
    });
}
void MainWindow::RefreshBindingLibrary(std::string selected) {
    struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(projectingBindingLibrary_);
    if (selected.empty()) selected = SelectedBindingSnapshot().value_or("");
    BindingSnapshotBox().Items().Clear();
    int index = -1;
    for (auto const& [name, text] : userData_.settings.bindingLibrary) {
        Controls::ComboBoxItem item;
        item.Content(box_value(to_hstring(name))); item.Tag(box_value(to_hstring(name)));
        BindingSnapshotBox().Items().Append(item);
        if (name == selected) index = static_cast<int>(BindingSnapshotBox().Items().Size() - 1);
    }
    BindingSnapshotBox().SelectedIndex(index);
    const bool valid = index >= 0 && userDataLoaded_ && !closing_;
    LoadBindingSnapshotButton().IsEnabled(valid); UpdateBindingSnapshotButton().IsEnabled(valid);
    RenameBindingSnapshotButton().IsEnabled(valid); DeleteBindingSnapshotButton().IsEnabled(valid);
    UndoBindingSnapshotButton().IsEnabled(bindingSnapshotUndo_.has_value() && userDataLoaded_ && !closing_);
}
void MainWindow::SaveBindingSnapshot(bool replace) {
    if (closing_ || !userDataLoaded_) return;
    auto name = replace ? SelectedBindingSnapshot() : std::optional<std::string>{to_string(BindingSnapshotName().Text())};
    if (!name) return;
    auto text = to_string(KeyBindingsEditor().Text());
    auto error = iima::BindingLibraryError(userData_.settings.bindingLibrary, *name, text, replace);
    if (!error.empty()) { BindingLibraryStatus().Text(to_hstring(error)); return; }
    auto& library = userData_.settings.bindingLibrary;
    if (replace) {
        bindingSnapshotUndo_ = std::pair{*name, library.at(*name)};
        bindingSnapshotUndoExpected_ = text;
    }
    library.insert_or_assign(*name, std::move(text));
    RefreshBindingLibrary(*name); MarkUserDataDirty();
    BindingLibraryStatus().Text(L"配置快照已更新，正在保存；当前生效快捷键未改变。");
}
void MainWindow::RenameBindingSnapshot() {
    if (closing_ || !userDataLoaded_) return;
    auto selected = SelectedBindingSnapshot();
    if (!selected) return;
    auto& library = userData_.settings.bindingLibrary;
    auto found = library.find(*selected);
    if (found == library.end()) { RefreshBindingLibrary(); return; }
    auto name = to_string(BindingSnapshotName().Text());
    if (name == *selected) return;
    auto candidate = library;
    auto text = found->second;
    candidate.erase(*selected);
    auto error = iima::BindingLibraryError(candidate, name, text, false);
    if (!error.empty()) { BindingLibraryStatus().Text(to_hstring(error)); return; }
    candidate.emplace(name, std::move(text)); library = std::move(candidate);
    RefreshBindingLibrary(name); MarkUserDataDirty();
    BindingLibraryStatus().Text(L"快照已重命名，正在保存；当前快捷键与编辑区不变。");
}
void MainWindow::DeleteBindingSnapshot() {
    if (closing_ || !userDataLoaded_) return;
    auto name = SelectedBindingSnapshot();
    if (!name) return;
    auto& library = userData_.settings.bindingLibrary;
    auto found = library.find(*name);
    if (found == library.end()) { RefreshBindingLibrary(); return; }
    bindingSnapshotUndo_ = std::pair{found->first, found->second};
    bindingSnapshotUndoExpected_.reset();
    library.erase(found); RefreshBindingLibrary(); MarkUserDataDirty();
    BindingLibraryStatus().Text(L"仅移除库内快照，可撤销；当前生效配置和外部文件未删除。");
}
void MainWindow::UndoBindingSnapshot() {
    if (closing_ || !userDataLoaded_ || !bindingSnapshotUndo_) return;
    auto const [name, text] = *bindingSnapshotUndo_;
    auto& library = userData_.settings.bindingLibrary;
    // Restoring an overwritten snapshot is explicit, but never clobber a new
    // value created after that overwrite. Track exactly the expected successor.
    auto found = library.find(name);
    if ((bindingSnapshotUndoExpected_ && (found == library.end() || found->second != *bindingSnapshotUndoExpected_)) ||
        (!bindingSnapshotUndoExpected_ && found != library.end())) {
        BindingLibraryStatus().Text(L"快照已再次变化，未用旧版本覆盖；请先保留当前编辑内容。"); return;
    }
    auto error = iima::BindingLibraryError(library, name, text, bindingSnapshotUndoExpected_.has_value());
    if (!error.empty()) { BindingLibraryStatus().Text(to_hstring(error)); return; }
    library.insert_or_assign(name, text); bindingSnapshotUndo_.reset(); bindingSnapshotUndoExpected_.reset();
    RefreshBindingLibrary(name); MarkUserDataDirty();
    BindingLibraryStatus().Text(L"旧快照已恢复，正在保存；尚未应用到当前快捷键。");
}
void MainWindow::LoadBindingSnapshot() {
    if (closing_ || !userDataLoaded_) return;
    auto name = SelectedBindingSnapshot();
    if (!name) return;
    auto found = userData_.settings.bindingLibrary.find(*name);
    if (found == userData_.settings.bindingLibrary.end()) { RefreshBindingLibrary(); return; }
    // This explicitly labelled action replaces the editor only, not the active
    // bindings. Export or save a draft before choosing this action if needed.
    KeyBindingsEditor().Text(to_hstring(found->second));
    BindingSnapshotName().Text(to_hstring(found->first));
    BindingLibraryStatus().Text(L"已替换编辑区；检查后点击“保存并应用”才切换当前快捷键。");
}
}
