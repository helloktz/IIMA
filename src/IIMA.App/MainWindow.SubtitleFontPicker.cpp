// IINA FontPickerWindowController and PlayerCore.chooseSubFont/setSubFont.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/FontCatalog.h"
#include <exception>
#include <memory>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
fire_and_forget MainWindow::ChooseSubtitleFont() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || !ApplySubtitleStyleButton().IsEnabled() ||
        session_->Snapshot().media != iima::MediaState::Loaded) co_return;
    pickerOpen_ = true;
    fontCatalogStop_ = std::stop_source{};
    auto stop = fontCatalogStop_.get_token();
    apartment_context ui;
    const auto epoch = navigationEpoch_;
    const auto entry = session_->Snapshot().entryId;
    const auto path = session_->Snapshot().path;
    const auto primary = session_->Snapshot().subtitleId;
    const auto secondary = session_->Snapshot().secondarySubtitleId;
    auto valid = [weak = get_weak(), epoch, entry, path, primary, secondary]() {
        auto self = weak.get();
        if (!self || self->closing_ || self->navigationEpoch_ != epoch) return false;
        auto const& state = self->session_->Snapshot();
        return state.media == iima::MediaState::Loaded && state.entryId == entry && state.path == path &&
            state.subtitleId == primary && state.secondarySubtitleId == secondary && self->ApplySubtitleStyleButton().IsEnabled();
    };
    try {
        auto const& values = session_->Snapshot().subtitleStyleValues;
        auto current = values.find("sub-font");
        auto initial = current == values.end() ? SubtitleFontText().Text() : to_hstring(current->second);
        const bool observed = current != values.end();
        std::vector<iima::FontFamilyEntry> families;
        std::exception_ptr failure;
        co_await resume_background();
        try { families = iima::FontCatalog::Enumerate(stop); }
        catch (...) { failure = std::current_exception(); }
        co_await ui;
        if (!stop.stop_requested() && valid()) {
            // Enumeration failure still permits explicit manual input, as IINA does.
            auto catalog = std::make_shared<const std::vector<iima::FontFamilyEntry>>(std::move(families));
            Controls::TextBlock info;
            info.Text(failure ? L"无法枚举本地字体，仍可手动输入名称。" :
                observed ? L"初值来自当前字幕字体。选择本地字体家族或手动输入名称。" : L"当前字体未知，初值来自编辑草稿。");
            info.TextWrapping(TextWrapping::Wrap);
            Controls::TextBox search;
            search.Header(box_value(L"搜索字体家族")); search.MaxLength(128);
            Controls::TextBox name;
            name.Header(box_value(L"字体名称（留空使用 sans-serif）")); name.MaxLength(512); name.Text(initial);
            Controls::TextBlock preview;
            preview.Text(L"字幕预览  Aa Bb 0123456789 中文"); preview.FontSize(24); preview.TextWrapping(TextWrapping::Wrap);
            Controls::TextBlock disclaimer;
            disclaimer.Text(L"仅应用字体名称，不改变字号或粗斜体。预览采用 Windows 字体回退，不能保证与 libass 渲染一致；ASS 内置样式可能覆盖选择。");
            disclaimer.TextWrapping(TextWrapping::Wrap);
            Controls::ListView list;
            list.Height(240); list.SelectionMode(Controls::ListViewSelectionMode::Single);
            auto rebuild = [catalog, list, name](hstring const& query) {
                list.Items().Clear();
                for (auto const& family : *catalog) {
                    if (!query.empty() && FindStringOrdinal(FIND_FROMSTART, family.displayName.c_str(), -1,
                        query.c_str(), -1, TRUE) < 0 && FindStringOrdinal(FIND_FROMSTART, family.name.c_str(), -1,
                        query.c_str(), -1, TRUE) < 0) continue;
                    Controls::ListViewItem row;
                    row.Content(box_value(hstring(family.displayName))); row.Tag(box_value(hstring(family.name)));
                    list.Items().Append(row);
                    if (name.Text() == hstring(family.name)) list.SelectedItem(row);
                }
            };
            // Only known installed family names reach XAML FontFamily. Arbitrary
            // manual text must never become a file/URI-based XAML font source.
            auto updatePreview = [catalog, preview](hstring const& text) {
                preview.FontFamily(Media::FontFamily(L"Segoe UI"));
                for (auto const& family : *catalog) if (text == hstring(family.name) &&
                    family.name.find_first_of(L"#:/\\,") == std::wstring::npos) {
                    preview.FontFamily(Media::FontFamily(hstring(family.name))); break;
                }
            };
            list.SelectionChanged([name](auto const& sender, auto const&) {
                auto selected = sender.template as<Controls::ListView>().SelectedItem().template try_as<Controls::ListViewItem>();
                if (selected) name.Text(unbox_value<hstring>(selected.Tag()));
            });
            search.TextChanged([rebuild](auto const& sender, auto const&) { rebuild(sender.template as<Controls::TextBox>().Text()); });
            name.TextChanged([updatePreview](auto const& sender, auto const&) { updatePreview(sender.template as<Controls::TextBox>().Text()); });
            rebuild(L""); updatePreview(name.Text());
            Controls::StackPanel content;
            content.Spacing(8);
            content.Children().Append(info); content.Children().Append(search);
            content.Children().Append(list); content.Children().Append(name);
            content.Children().Append(preview); content.Children().Append(disclaimer);
            Controls::ScrollViewer scroll; scroll.MaxHeight(560); scroll.Content(content);
            Controls::ContentDialog dialog;
            subtitleFontDialog_ = dialog;
            dialog.XamlRoot(Root().XamlRoot()); dialog.Title(box_value(L"字幕字体")); dialog.Content(scroll);
            dialog.PrimaryButtonText(L"应用字体名称"); dialog.CloseButtonText(L"取消");
            dialog.DefaultButton(Controls::ContentDialogButton::Close);
            dialog.PrimaryButtonClick([valid, name, info](auto const&, Controls::ContentDialogButtonClickEventArgs const& args) {
                auto text = to_string(name.Text());
                if (!valid()) { args.Cancel(true); info.Text(L"媒体或字幕选择已变化，请取消后重新打开。"); }
                else if (text.size() > 1024 || text.find_first_of("\r\n") != std::string::npos || text.find('\0') != std::string::npos) {
                    args.Cancel(true); info.Text(L"请输入不含换行的有效字体名称。");
                }
            });
            auto result = co_await dialog.ShowAsync();
            if (result == Controls::ContentDialogResult::Primary && valid()) {
                iima::SubtitleStyle style;
                style.font = name.Text().empty() ? "sans-serif" : to_string(name.Text());
                session_->ApplySubtitleStyle(style, {"sub-font"});
                SubtitleFontText().Text(to_hstring(style.font)); // draft only; observed state remains authoritative
            }
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    catch (...) { if (!closing_) ShowError(L"无法选择字幕字体。"); }
    subtitleFontDialog_ = nullptr;
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}
