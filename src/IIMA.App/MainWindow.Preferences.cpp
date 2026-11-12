// Preference defaults originate in IINA Preference.swift; Windows controls and
// explicit commit semantics replace Cocoa bindings. GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool HasApplicationAdapter(std::string_view key) {
    static const std::set<std::string_view> keys{
        "pauseWhenInactive", "pauseWhenMinimized",
        "alwaysFloatOnTop", "fullScreenWhenOpen", "playWhenEnteringFullScreen", "pauseWhenLeavingFullScreen",
        "enableOSD", "disableOSDPauseResumeMsgs", "disableOSDSpeedMsg", "osdAutoHideTimeout", "osdTextSize"
    };
    return keys.contains(key);
}
}
namespace winrt::IIMA::implementation {
void MainWindow::PopulateAdvancedSettings() {
    auto panel = AdvancedSettingsPanel();
    panel.Children().Clear();
    Controls::TextBlock explanation;
    explanation.Text(L"播放器选项采用上游键名。即时项会提交至内核；标注“下次打开”的项不会改写当前媒体。控制值表示偏好，不代表内核已确认。");
    explanation.TextWrapping(TextWrapping::Wrap);
    panel.Children().Append(explanation);
    auto weak = get_weak();
    for (auto const& descriptor : iima::settings::PreferenceSchema()) {
        const auto sessionKeys = iima::settings::SessionPreferenceKeys();
        const bool sessionAdapter = std::find(sessionKeys.begin(), sessionKeys.end(), descriptor.key) != sessionKeys.end();
        if (!sessionAdapter && !HasApplicationAdapter(descriptor.key)) continue;
        auto key = std::string(descriptor.key);
        auto label = to_hstring(key);
        if (descriptor.timing == iima::settings::ApplyTiming::NextMedia)
            label = label + L"（下次打开）";
        auto value = preferences_.Get(key);
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
            box.SelectionChanged([weak, key](auto const& sender, auto const&) {
                if (auto self = weak.get(); self && !self->closing_)
                    if (auto item = sender.template as<Controls::ComboBox>().SelectedItem().template try_as<Controls::ComboBoxItem>())
                        self->ApplyAdvancedSetting(key, unbox_value<std::int64_t>(item.Tag()));
            });
            panel.Children().Append(box);
        } else if (auto boolean = std::get_if<bool>(&value)) {
            Controls::CheckBox box; box.Content(box_value(label)); box.IsChecked(*boolean);
            auto changed = [weak, key](auto const& sender, auto const&) {
                if (auto self = weak.get(); self && !self->closing_) {
                    auto checked = sender.template as<Controls::CheckBox>().IsChecked();
                    if (checked) self->ApplyAdvancedSetting(key, checked.Value());
                }
            };
            box.Checked(changed); box.Unchecked(changed);
            panel.Children().Append(box);
        } else if (auto text = std::get_if<std::string>(&value)) {
            Controls::StackPanel row;
            Controls::TextBox box; box.Header(box_value(label)); box.Text(to_hstring(*text)); box.MaxLength(4096);
            Controls::Button apply; apply.Content(box_value(L"应用"));
            apply.Click([weak, key, box](auto const&, auto const&) {
                if (auto self = weak.get(); self && !self->closing_) self->ApplyAdvancedSetting(key, to_string(box.Text()));
            });
            row.Children().Append(box); row.Children().Append(apply); panel.Children().Append(row);
        } else {
            Controls::NumberBox box; box.Header(box_value(label));
            bool integral = std::holds_alternative<std::int64_t>(value);
            if (descriptor.minimum) box.Minimum(*descriptor.minimum);
            if (descriptor.maximum) box.Maximum(*descriptor.maximum);
            box.Value(integral ? static_cast<double>(std::get<std::int64_t>(value)) : std::get<double>(value));
            box.ValueChanged([weak, key, integral](auto const&, Controls::NumberBoxValueChangedEventArgs const& args) {
                auto v = args.NewValue();
                if (!std::isfinite(v)) return;
                if (auto self = weak.get(); self && !self->closing_) {
                    if (integral) {
                        if (std::trunc(v) != v || v < -9007199254740991.0 || v > 9007199254740991.0) {
                            self->ShowError(L"此偏好必须是整数。"); return;
                        }
                        self->ApplyAdvancedSetting(key, static_cast<std::int64_t>(v));
                    } else self->ApplyAdvancedSetting(key, v);
                }
            });
            panel.Children().Append(box);
        }
    }
}
void MainWindow::ApplyAdvancedSetting(std::string const& key, iima::settings::PreferenceValue value) {
    if (closing_ || !userDataLoaded_) return;
    std::string error;
    if (!preferences_.Set(key, std::move(value), error)) { ShowError(to_hstring(error)); return; }
    userData_.settings.advancedPreferences = preferences_.Overrides();
    session_->ApplyAdvancedPreferences(preferences_);
    ApplyWindowPreferences();
    MarkUserDataDirty();
}
void MainWindow::ApplyWindowPreferences() {
    try {
        if (auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
            presenter.IsAlwaysOnTop(preferences_.Boolean("alwaysFloatOnTop"));
        OSDTitle().FontSize(preferences_.Number("osdTextSize"));
        if (!preferences_.Boolean("enableOSD")) {
            OSDBorder().Visibility(Visibility::Collapsed);
            if (osdTimer_) osdTimer_.Stop();
        }
    } catch (hresult_error const& error) { ShowError(error.message()); }
}
}
