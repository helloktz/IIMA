// IINA SettingsPageKeyBindings.ConfigEditor add/edit/remove/search operations.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <set>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireBindingEditor() {
    auto weak = get_weak();
    LoadBindingRowsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->LoadBindingRows();
    });
    WriteBindingRowsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->WriteBindingRows();
    });
    BindingRowSearch().TextChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->RefreshBindingRows();
    });
    BindingRowsList().SelectionChanged([weak](auto const&, auto const&) {
        auto self = weak.get();
        if (!self || self->projectingBindingRows_ || !self->bindingRowDraft_) return;
        self->UpdateBindingRowFields();
    });
    auto wire = [weak](Controls::Button button, int action) {
        button.Click([weak, action](auto const&, auto const&) {
            if (auto self = weak.get()) self->EditBindingRow(action);
        });
    };
    wire(AddBindingRowButton(), 0); wire(ReplaceBindingRowButton(), 1);
    wire(RemoveBindingRowButton(), 2); wire(MoveBindingRowUpButton(), 3);
    wire(MoveBindingRowDownButton(), 4);
}
std::optional<std::size_t> MainWindow::SelectedBindingRow() {
    if (!bindingRowDraft_) return {};
    auto item = BindingRowsList().SelectedItem().try_as<Controls::ListViewItem>();
    if (!item) return {};
    auto row = unbox_value<std::uint64_t>(item.Tag());
    return row < bindingRowDraft_->Mappings().size() ? std::optional<std::size_t>{static_cast<std::size_t>(row)} : std::nullopt;
}
void MainWindow::UpdateBindingRowFields() {
    auto row = SelectedBindingRow();
    if (!row) {
        BindingRowKey().Text(L""); BindingRowAction().Text(L""); BindingRowComment().Text(L""); return;
    }
    auto const& mapping = bindingRowDraft_->Mappings()[*row];
    BindingRowKey().Text(to_hstring(mapping.RawKey()));
    BindingRowAction().Text(to_hstring(mapping.ReadableAction()));
    BindingRowComment().Text(to_hstring(mapping.Comment().value_or("")));
}
void MainWindow::LoadBindingRows() {
    if (closing_) return;
    auto text = to_string(KeyBindingsEditor().Text());
    if (text.size() > 256 * 1024 || text.find('\0') != std::string::npos) {
        BindingRowStatus().Text(iima::loc::Text(L"TextExceeding256KiBOrContainingNUL")); return;
    }
    auto parsed = iima::ParseInputConf(text);
    if (!parsed.issues.empty()) {
        auto const& issue = parsed.issues.front();
        BindingRowStatus().Text(iima::loc::Text(L"IDonTThinkSo") + to_hstring(issue.line) + iima::loc::Text(L"Lines") + to_hstring(issue.message)); return;
    }
    if (parsed.mappings.size() > iima::BindingConfiguration::MaximumMappings) {
        BindingRowStatus().Text(iima::loc::Text(L"UpTo10000Bindings")); return;
    }
    bindingRowDraft_ = std::make_unique<iima::BindingConfiguration>("editor draft", std::move(parsed.mappings));
    bindingRowSource_ = std::move(text);
    RefreshBindingRows();
}
void MainWindow::RefreshBindingRows(std::optional<std::size_t> selected) {
    if (closing_ || projectingBindingRows_) return;
    if (!selected) selected = SelectedBindingRow();
    struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(projectingBindingRows_);
    BindingRowsList().Items().Clear();
    if (!bindingRowDraft_) { BindingRowStatus().Text(iima::loc::Text(L"ReadTheBindingFirstFromTheText")); return; }
    std::set<std::size_t> conflicts, winners;
    auto groups = bindingRowDraft_->Conflicts();
    for (auto const& conflict : groups) {
        conflicts.insert(conflict.rows.begin(), conflict.rows.end()); winners.insert(conflict.winningRow);
    }
    auto rows = bindingRowDraft_->Search(to_string(BindingRowSearch().Text()));
    for (auto row : rows) {
        auto const& mapping = bindingRowDraft_->Mappings()[row];
        auto label = std::to_string(row + 1) + ". " + mapping.RawKey() + " → " + mapping.ReadableAction();
        if (conflicts.contains(row)) label += winners.contains(row) ? iima::loc::Utf8(L"RepeatedKeysFinalPriority") : iima::loc::Utf8(L"CoveredByTheLatterItem");
        Controls::ListViewItem item;
        item.Tag(box_value(static_cast<std::uint64_t>(row))); item.Content(box_value(to_hstring(label)));
        BindingRowsList().Items().Append(item);
        if (selected == row) BindingRowsList().SelectedItem(item);
    }
    UpdateBindingRowFields();
    BindingRowStatus().Text(iima::loc::Text(L"Show") + to_hstring(rows.size()) + L" / " + to_hstring(bindingRowDraft_->Mappings().size()) +
        iima::loc::Text(L"BarRepeatKeys") + to_hstring(groups.size()) + iima::loc::Text(L"TheChangesAreOnlyInLineEdit"));
}
void MainWindow::EditBindingRow(int action) {
    if (closing_ || !bindingRowDraft_) return;
    auto row = SelectedBindingRow();
    if (action != 0 && !row) { BindingRowStatus().Text(iima::loc::Text(L"PleaseSelectABindingFirst")); return; }
    auto candidate = *bindingRowDraft_;
    iima::BindingEditResult result;
    std::optional<std::size_t> selection = row;
    if (action == 0 || action == 1) {
        iima::KeyMapping mapping(to_string(BindingRowKey().Text()), "");
        mapping.SetRawAction(to_string(BindingRowAction().Text())); // Recognizes @iina.
        auto comment = to_string(BindingRowComment().Text());
        if (!comment.empty()) mapping.SetComment(comment);
        if (action == 0) {
            selection = candidate.Mappings().size();
            result = candidate.Insert(*selection, std::move(mapping));
        } else result = candidate.Replace(*row, std::move(mapping));
    } else if (action == 2) {
        auto value = *row;
        result = candidate.Remove(std::span(&value, 1)); selection.reset();
    } else if (action == 3 || action == 4) {
        if ((action == 3 && !*row) || (action == 4 && *row + 1 >= candidate.Mappings().size())) return;
        auto value = *row;
        result = candidate.Move(std::span(&value, 1), action == 3 ? *row - 1 : *row + 2);
        selection = action == 3 ? *row - 1 : *row + 1;
    } else return;
    if (!result) { BindingRowStatus().Text(to_hstring(result.message)); return; }
    if (candidate.Serialize().size() > 256 * 1024) {
        BindingRowStatus().Text(iima::loc::Text(L"GeneratingTextWillExceed256KiBAnd")); return;
    }
    *bindingRowDraft_ = std::move(candidate);
    RefreshBindingRows(selection);
}
void MainWindow::WriteBindingRows() {
    if (closing_ || !bindingRowDraft_) return;
    if (to_string(KeyBindingsEditor().Text()) != bindingRowSource_) {
        BindingRowStatus().Text(iima::loc::Text(L"TextAreaHasChangedNotCoveredPlease")); return;
    }
    auto text = bindingRowDraft_->Serialize();
    if (text.size() > 256 * 1024) { BindingRowStatus().Text(iima::loc::Text(L"GeneratesTextExceeding256KiB")); return; }
    // GenerateInputConf normalizes whitespace and keeps per-mapping comments,
    // not standalone comments/blank lines. The button explicitly discloses it.
    KeyBindingsEditor().Text(to_hstring(text)); bindingRowSource_ = std::move(text);
    BindingRowStatus().Text(iima::loc::Text(L"AStandardizedTextHasBeenGeneratedAnd"));
}
}
