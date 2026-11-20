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
            info.Text(failure ? iima::loc::Text(L"CouldNotListInstalledFontsYouCan") :
                observed ? iima::loc::Text(L"TheOpeningValueIsDerivedFromThe") : iima::loc::Text(L"TheCurrentFontIsUnknownTheOpening"));
            info.TextWrapping(TextWrapping::Wrap);
            Controls::TextBox search;
            search.Header(box_value(iima::loc::Text(L"SearchFontFamily"))); search.MaxLength(128);
            Controls::TextBox name;
            name.Header(box_value(iima::loc::Text(L"FontNameSansSerifLeftEmpty"))); name.MaxLength(512); name.Text(initial);
            Controls::TextBlock preview;
            preview.Text(iima::loc::Text(L"SubtitlePreviewAaBb0123456789Chinese")); preview.FontSize(24); preview.TextWrapping(TextWrapping::Wrap);
            Controls::TextBlock disclaimer;
            disclaimer.Text(iima::loc::Text(L"ApplyFontNamesOnlyWithoutChangingFonts"));
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
            dialog.XamlRoot(Root().XamlRoot()); dialog.Title(box_value(iima::loc::Text(L"SubtitleFont"))); dialog.Content(scroll);
            dialog.PrimaryButtonText(iima::loc::Text(L"ApplyFontName")); dialog.CloseButtonText(iima::loc::Text(L"Cancel"));
            dialog.DefaultButton(Controls::ContentDialogButton::Close);
            dialog.PrimaryButtonClick([valid, name, info](auto const&, Controls::ContentDialogButtonClickEventArgs const& args) {
                auto text = to_string(name.Text());
                if (!valid()) { args.Cancel(true); info.Text(iima::loc::Text(L"TheChoiceOfMediaOrSubtitlesHas")); }
                else if (text.size() > 1024 || text.find_first_of("\r\n") != std::string::npos || text.find('\0') != std::string::npos) {
                    args.Cancel(true); info.Text(iima::loc::Text(L"EnterAValidFontNameThatDoes"));
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
    catch (...) { if (!closing_) ShowError(iima::loc::Text(L"CouldNotSelectTheSubtitleFont")); }
    subtitleFontDialog_ = nullptr;
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}
