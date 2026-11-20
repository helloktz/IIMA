// Preference defaults originate in IINA Preference.swift; Windows controls and
// explicit commit semantics replace Cocoa bindings. GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Geometry/WindowGeometry.h"
#include "../IIMA.Core/Matching/SubtitleSearchPath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <functional>
#include <memory>
#include <utility>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
// A programmatic editor refresh must not generate a second preference command.
struct EditorRefreshScope {
    std::shared_ptr<bool> flag;
    explicit EditorRefreshScope(std::shared_ptr<bool> value) : flag(std::move(value)) { *flag = true; }
    ~EditorRefreshScope() { *flag = false; }
};
bool HasApplicationAdapter(std::string_view key) {
    static const std::set<std::string_view> keys{
        "arrowBtnAction", "showChapterPos", "prefetchPlaylistVideoDuration", "playlistShowMetadata", "playlistShowMetadataInMusicMode", "playlistAutoAdd", "subAutoLoadIINA", "subAutoLoadPriorityString", "subAutoLoadSearchPath", "initialWindowSizePosition", "enableInitialVolume", "initialVolume", "alwaysOpenInNewWindow", "groupSimultaneousOpensInPlaylist", "allowDuplicatePlayers",
        "autoSwitchToMusicMode", "musicModeShowPlaylist", "musicModeShowAlbumArt",
        "resizeWindowTiming", "resizeWindowOption", "usePhysicalResolution", "unlockWindowAspectRatio", "preventScreenSaver", "allowScreenSaverForAudio", "pauseWhenPip", "pauseWhenInactive", "pauseWhenMinimized", "pauseWhenGoesToSleep", "enableThumbnailPreview", "thumbnailWidth", "maxThumbnailPreviewCacheSize",
        "displayTimeAndBatteryInFullScreen", "alwaysFloatOnTop", "fullScreenWhenOpen", "playWhenEnteringFullScreen", "pauseWhenLeavingFullScreen",
        "singleClickAction", "doubleClickAction", "rightClickAction", "verticalScrollAction", "horizontalScrollAction", "disablePlaySliderScrolling", "disableVolumeSliderScrolling",
        "relativeSeekAmount", "playbackSpeedScrollAmount", "wheelExactSeek", "enableControlBarAutoHide", "controlBarAutoHideTimeout", "controlBarToolbarButtons", "oscPosition", "controlBarPositionHorizontal", "controlBarPositionVertical", "controlBarStickToCenter",
        "showRemainingTime", "scaleRemainingTime", "timeDisplayPrecision", "disableOSDFileStartMsg", "disableOSDSeekMsg",
        "disableAnimations", "showBufferingThrobber", "showSeekingThrobber", "useExactSeek", "screenShotTemplate", "screenShotFormat", "screenshotSaveToFile", "screenshotCopyToClipboard", "screenshotShowPreview", "enableOSD", "disableOSDPauseResumeMsgs", "disableOSDSpeedMsg", "osdAutoHideTimeout", "osdTextSize"
    };
    return keys.contains(key);
}
}
namespace winrt::IIMA::implementation {
void MainWindow::PopulateAdvancedSettings() {
    auto panel = AdvancedSettingsPanel();
    panel.Children().Clear();
    advancedSettingsRows_.clear(); advancedSettingsIndex_.clear();
    Controls::TextBlock explanation;
    explanation.Text(iima::loc::Text(L"ThePlayerOptionUsesAnUpstreamKeyname"));
    explanation.TextWrapping(TextWrapping::Wrap);
    panel.Children().Append(explanation);
    auto weak = get_weak();
    for (auto const& descriptor : iima::settings::PreferenceSchema()) {
        const auto sessionKeys = iima::settings::SessionPreferenceKeys();
        const bool sessionAdapter = std::find(sessionKeys.begin(), sessionKeys.end(), descriptor.key) != sessionKeys.end();
        if (!sessionAdapter && !HasApplicationAdapter(descriptor.key)) continue;
        auto key = std::string(descriptor.key);
        auto label = to_hstring(key);
        if(key=="arrowBtnAction") label = label + iima::loc::Text(L"OSCLeftAndRightArrows0Times");
        if(key=="showChapterPos") label = label + iima::loc::Text(L"ShowsChapterScaleUpTo512");
        if (descriptor.timing == iima::settings::ApplyTiming::NextMedia)
            label = label + iima::loc::Text(L"NextTimeOpen");
        else if (descriptor.timing == iima::settings::ApplyTiming::NextPlayer)
            label = label + iima::loc::Text(L"NextNewPlayer");
        auto value = preferences_.Get(key);
        auto refreshing = std::make_shared<bool>(false);
        std::function<void(iima::settings::PreferenceValue const&)> refreshEditor;
        if (!descriptor.choices.empty()) {
            Controls::ComboBox box;
            box.Header(box_value(label)); box.HorizontalAlignment(HorizontalAlignment::Stretch);
            int selected = -1;
            for (auto const& choice : descriptor.choices) {
                Controls::ComboBoxItem item;
                item.Content(box_value(to_hstring(choice.token))); item.Tag(box_value(choice.value));
                box.Items().Append(item);
                if (choice.value == std::get<std::int64_t>(value)) selected = static_cast<int>(box.Items().Size() - 1);
            }
            box.SelectedIndex(selected);
            box.SelectionChanged([weak, key, refreshing](auto const& sender, auto const&) {
                if (*refreshing) return;
                if (auto self = weak.get(); self && !self->closing_)
                    if (auto item = sender.template as<Controls::ComboBox>().SelectedItem().template try_as<Controls::ComboBoxItem>())
                        self->ApplyAdvancedSetting(key, unbox_value<std::int64_t>(item.Tag()));
            });
            refreshEditor = [box](auto const& initial) {
                const auto wanted = std::get<std::int64_t>(initial);
                for (std::uint32_t i = 0; i < box.Items().Size(); ++i) {
                    auto item = box.Items().GetAt(i).template as<Controls::ComboBoxItem>();
                    if (unbox_value<std::int64_t>(item.Tag()) == wanted) {
                        box.SelectedIndex(static_cast<int>(i)); return;
                    }
                }
                box.SelectedIndex(-1);
            };
            panel.Children().Append(box);
        } else if (auto boolean = std::get_if<bool>(&value)) {
            Controls::CheckBox box; box.Content(box_value(label)); box.IsChecked(*boolean);
            auto changed = [weak, key, refreshing](auto const& sender, auto const&) {
                if (*refreshing) return;
                if (auto self = weak.get(); self && !self->closing_) {
                    auto checked = sender.template as<Controls::CheckBox>().IsChecked();
                    if (checked) self->ApplyAdvancedSetting(key, checked.Value());
                }
            };
            box.Checked(changed); box.Unchecked(changed);
            refreshEditor = [box](auto const& initial) { box.IsChecked(std::get<bool>(initial)); };
            panel.Children().Append(box);
        } else if (auto text = std::get_if<std::string>(&value)) {
            Controls::StackPanel row;
            Controls::TextBox box; box.Header(box_value(label)); box.Text(to_hstring(*text)); box.MaxLength(4096);
            if (key == "initialWindowSizePosition") {
                box.MaxLength(128);
                box.PlaceholderText(iima::loc::Text(L"Text1000X6002030Or80"));
                Automation::AutomationProperties::SetHelpText(box,
                    iima::loc::Text(L"PhysicalPixelWindowGeometryWidthIsHigher"));
            }
            Controls::Button apply; apply.Content(box_value(iima::loc::Text(L"Apply")));
            apply.Click([weak, key, box](auto const&, auto const&) {
                if (auto self = weak.get(); self && !self->closing_) self->ApplyAdvancedSetting(key, to_string(box.Text()));
            });
            refreshEditor = [box](auto const& initial) { box.Text(to_hstring(std::get<std::string>(initial))); };
            row.Children().Append(box); row.Children().Append(apply); panel.Children().Append(row);
        } else {
            Controls::NumberBox box; box.Header(box_value(label));
            bool integral = std::holds_alternative<std::int64_t>(value);
            if (descriptor.minimum) box.Minimum(*descriptor.minimum);
            if (descriptor.maximum) box.Maximum(*descriptor.maximum);
            box.Value(integral ? static_cast<double>(std::get<std::int64_t>(value)) : std::get<double>(value));
            box.ValueChanged([weak, key, integral, refreshing](auto const&, Controls::NumberBoxValueChangedEventArgs const& args) {
                if (*refreshing) return;
                auto v = args.NewValue();
                if (!std::isfinite(v)) return;
                if (auto self = weak.get(); self && !self->closing_) {
                    if (integral) {
                        if (std::trunc(v) != v || v < -9007199254740991.0 || v > 9007199254740991.0) {
                            self->ShowError(iima::loc::Text(L"ThisPreferenceMustBeInteger")); return;
                        }
                        self->ApplyAdvancedSetting(key, static_cast<std::int64_t>(v));
                    } else self->ApplyAdvancedSetting(key, v);
                }
            });
            refreshEditor = [box, integral](auto const& initial) {
                box.Value(integral ? static_cast<double>(std::get<std::int64_t>(initial)) : std::get<double>(initial));
            };
            panel.Children().Append(box);
        }
        // Wrap only this editor. Resetting one field must not rebuild the other
        // rows, lose their unsubmitted text, or discard the current search.
        auto editor = panel.Children().GetAt(panel.Children().Size() - 1);
        panel.Children().RemoveAtEnd();
        Controls::StackPanel settingsRow;
        settingsRow.Spacing(4);
        settingsRow.Children().Append(editor);
        if (key == "subAutoLoadSearchPath") {
            Controls::TextBlock hint;
            hint.Text(iima::loc::Text(L"NextSingleFileOpensSeparatedBySemicolons"));
            hint.TextWrapping(TextWrapping::Wrap);
            settingsRow.Children().Append(hint);
        }
        Controls::Button reset;
        reset.Content(box_value(iima::loc::Text(L"RestoreThisDefault")));
        Automation::AutomationProperties::SetName(reset, label + iima::loc::Text(L"RestoreDefaultValue"));
        Controls::ToolTipService::SetToolTip(reset, box_value(
            iima::loc::Text(L"OnlyThisItemWillBeRestoredAnd")));
        // Keep enabled even if there is no stored override: a string editor can
        // contain an unapplied draft that the user wants to discard.
        reset.Click([weak, key, refreshing, refreshEditor](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || *refreshing) return;
            auto descriptor = iima::settings::FindPreference(key);
            if (!descriptor || !refreshEditor) return;
            try {
                // The descriptor is process-lifetime storage. Set(default)
                // removes the override; use the normal application/persistence
                // path so all per-key side effects remain identical to editing.
                EditorRefreshScope scope(refreshing);
                self->ApplyAdvancedSetting(key, descriptor->initial);
                refreshEditor(self->preferences_.Get(key));
            } catch (hresult_error const& error) {
                // The preference may already be committed even if a native UI
                // update failed. Do not roll back or submit another mpv command.
                self->ShowError(error.message());
            }
        });
        settingsRow.Children().Append(reset);
        panel.Children().Append(settingsRow);
        auto row=panel.Children().GetAt(panel.Children().Size()-1).as<FrameworkElement>();
        std::string searchable=key+" "+std::string(iima::settings::PreferenceAreaTitle(descriptor.area))+" "+
            std::string(descriptor.mpvProperty);
        if(key=="arrowBtnAction") searchable+=iima::loc::Utf8(L"OSCLeftAndRightArrowMultiSpeed");
        if(key=="showChapterPos") searchable+=iima::loc::Utf8(L"ChapterMarks");
        for (auto const& choice:descriptor.choices) searchable+=" "+std::string(choice.token);
        searchable += descriptor.timing == iima::settings::ApplyTiming::NextMedia ? iima::loc::Utf8(L"NextMediaNextTime") :
            descriptor.timing == iima::settings::ApplyTiming::NextPlayer ? iima::loc::Utf8(L"NextNewPlayerStartupInitialPlayer") : iima::loc::Utf8(L"Immediate");
        advancedSettingsRows_.push_back(row);
        advancedSettingsIndex_.push_back({key,descriptor.area,std::move(searchable)});
    }
    FilterAdvancedSettings();
}
void MainWindow::ApplyAdvancedSetting(std::string const& key, iima::settings::PreferenceValue value) {
    if (key == "screenShotTemplate") {
        auto text=std::get_if<std::string>(&value);
        if (!text || !iima::ValidateScreenshotFilenameTemplate(*text) ||
            !iima::PlanScreenshotFilename("IIMA",1,iima::ScreenshotFormat::Png,*text)) {
            ShowError(iima::loc::Text(L"TheScreenshotTemplateIsInvalidOnlyF")); return;
        }
    }
    if (closing_ || !userDataLoaded_) return;
    if (key == "initialWindowSizePosition") {
        auto geometry = std::get_if<std::string>(&value);
        if (!geometry || geometry->size() > 128 ||
            (!geometry->empty() && !iima::WindowGeometry::Parse(*geometry))) {
            ShowError(iima::loc::Text(L"WindowGeometryFormatIsInvalidExample1000"));
            return;
        }
    }
    if (key == "subAutoLoadSearchPath") {
        auto text = std::get_if<std::string>(&value);
        std::wstring issue;
        if (!text || !iima::ParseSubtitleSearchPaths(to_hstring(*text), issue)) {
            ShowError(iima::loc::Text(issue.empty() ? L"SubtitleDirectoryFormatIsInvalidWindowsPath" : issue.c_str()));
            return;
        }
    }
    if (key=="oscPosition" || key.starts_with("controlBarPosition") || key=="controlBarStickToCenter") CancelControlBarDrag();
    std::string error;
    if (!preferences_.Set(key, std::move(value), error)) { ShowError(to_hstring(error)); return; }
    if (key == "playlistAutoAdd" || key == "subAutoLoadIINA" || key == "subAutoLoadPriorityString" || key == "subAutoLoadSearchPath")
        CancelAutomaticFiles(); // Changes apply to the next explicit single-file open.
    userData_.settings.advancedPreferences = preferences_.Overrides();
    session_->ApplyAdvancedPreferences(preferences_, key == "audioDevice");
    UpdateControlBarPolicy();
    RefreshAccessibility();
    UpdateTimeDisplay(session_->Snapshot());
    CancelMouseClick();
    ApplyWindowPreferences();
    UpdateMediaIntegration();
    if (key == "prefetchPlaylistVideoDuration") UpdatePlaylistDurations(true);
    if (key == "playlistShowMetadata" || key == "playlistShowMetadataInMusicMode") UpdatePlaylistMetadata();
    if (key == "showChapterPos") UpdateChapterMarkers();
    if (key == "screenShotFolder") UpdateScreenshotSettings();
    if (key == "screenshotShowPreview" && !preferences_.Boolean(key)) HideScreenshotPreview();
    if (key == "enableThumbnailPreview" || key == "thumbnailWidth" || key == "maxThumbnailPreviewCacheSize") {
        CancelThumbnailPreview(); thumbnailMemory_.clear();
    }
    if (key == "autoSwitchToMusicMode" || key.starts_with("musicMode")) { UpdateMusicMode(); UpdatePresentationLayout(); }
    MarkUserDataDirty();
    FilterAdvancedSettings();
}
void MainWindow::ApplyWindowPreferences() {
    try {
        UpdateControlBarToolbar();
        UpdateFullscreenInformation();
        UpdateWindowAspectConstraint();
        RefreshPlayerMenus();
        if (auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
            presenter.IsAlwaysOnTop(preferences_.Boolean("alwaysFloatOnTop"));
        OSDTitle().FontSize(preferences_.Number("osdTextSize"));
        if (!preferences_.Boolean("enableOSD")) {
            HideScreenshotPreview();
            OSDBorder().Visibility(Visibility::Collapsed);
            if (osdTimer_) osdTimer_.Stop();
        }
    } catch (hresult_error const& error) { ShowError(error.message()); }
}
}
