// IINA FilterWindowController.NewFilterSheetViewController showSettings /
// sheetAddBtnAction and FilterPresets. Copyright (c) 2017 lhc. GPL-3.0.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
#include <limits>
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
hstring PresetLabel(std::string const& name) {
    static const std::map<std::string, std::wstring, std::less<>> labels{
        {"crop", L"裁剪"}, {"expand", L"扩展画布"}, {"sharpen", L"锐化"},
        {"blur", L"模糊"}, {"delogo", L"去标志"}, {"negative", L"反色"},
        {"vflip", L"上下翻转"}, {"hflip", L"左右镜像"}, {"lut3d", L"三维LUT"},
        {"custom_mpv", L"自定义mpv滤镜"}, {"custom_ffmpeg", L"自定义FFmpeg滤镜"}};
    auto found = labels.find(name);
    return found == labels.end() ? to_hstring(name) : hstring{found->second};
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireFilterPresets() {
    auto weak = get_weak();
    FilterPresetBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->projectingFilterPresets_)
            self->ShowFilterPresetParameters();
    });
    ResetFilterPresetButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->ShowFilterPresetParameters();
    });
    GenerateFilterPresetButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->GenerateFilterPreset();
    });
    ResetFilterPresetCatalog();
}
void MainWindow::ResetFilterPresetCatalog() {
    // A previously generated expression belongs to the old audio/video catalog.
    // Preserve independently edited manual expressions, but never silently apply
    // an unchanged generated video preset to the newly selected audio chain.
    if (!lastGeneratedFilter_.empty() && to_string(ChainFilterExpression().Text()) == lastGeneratedFilter_)
        ChainFilterExpression().Text(L"");
    lastGeneratedFilter_.clear();
    projectingFilterPresets_ = true;
    filterPresets_ = FilterTypeBox().SelectedIndex() == 1 ?
        iima::FilterPreset::AudioPresets() : iima::FilterPreset::VideoPresets();
    FilterPresetBox().Items().Clear();
    for (auto const& preset : filterPresets_) FilterPresetBox().Items().Append(box_value(PresetLabel(preset.name)));
    FilterPresetBox().SelectedIndex(filterPresets_.empty() ? -1 : 0);
    projectingFilterPresets_ = false;
    ShowFilterPresetParameters();
}
void MainWindow::ShowFilterPresetParameters() {
    ++filterPresetEpoch_;
    filterPresetInputs_.clear(); FilterPresetParameters().Children().Clear();
    auto selected = FilterPresetBox().SelectedIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= filterPresets_.size()) return;
    auto const& preset = filterPresets_[selected];
    for (auto const& name : preset.parameterOrder) {
        auto const& parameter = preset.parameters.at(name);
        FrameworkElement control{nullptr};
        using Type = iima::FilterParameter::Type;
        switch (parameter.type) {
        case Type::Text: {
            Controls::TextBox box;
            box.Header(box_value(preset.name == "lut3d" && name == "file" ? hstring{L"file（原始文件路径，无需转义）"} : to_hstring(name))); box.MaxLength(16384);
            box.Text(to_hstring(parameter.defaultValue.StringValue()));
            control = box; break;
        }
        case Type::Integer: case Type::Float: {
            Controls::NumberBox box;
            box.Header(box_value(to_hstring(name)));
            box.Minimum(parameter.type == Type::Integer ? parameter.minimumInt : parameter.minimum);
            box.Maximum(parameter.type == Type::Integer ? parameter.maximumInt : parameter.maximum);
            box.SmallChange(parameter.type == Type::Integer ? parameter.step : 0.1);
            box.LargeChange(parameter.type == Type::Integer ? parameter.step : 0.5);
            box.Value(parameter.type == Type::Integer ? parameter.defaultValue.IntValue() : parameter.defaultValue.FloatValue());
            box.SpinButtonPlacementMode(Controls::NumberBoxSpinButtonPlacementMode::Compact);
            control = box; break;
        }
        case Type::Choice: {
            Controls::ComboBox box;
            box.Header(box_value(to_hstring(name)));
            box.HorizontalAlignment(HorizontalAlignment::Stretch);
            int index = -1;
            for (auto const& choice : parameter.choices) {
                box.Items().Append(box_value(to_hstring(choice)));
                if (choice == parameter.defaultValue.StringValue()) index = static_cast<int>(box.Items().Size() - 1);
            }
            box.SelectedIndex(index); control = box; break;
        }
        }
        filterPresetInputs_.emplace(name, control);
        FilterPresetParameters().Children().Append(control);
    }
    if (preset.name == "lut3d") {
        Controls::Button choose;
        choose.Content(box_value(L"选择 LUT 文件…"));
        auto weak = get_weak();
        choose.Click([weak](auto const&, auto const&) {
            if (auto self = weak.get()) self->PickFilterLutFile();
        });
        FilterPresetParameters().Children().Append(choose);
    }
    FilterPresetStatus().Text(L"参数来自固定IINA预设。生成仅填入表达式，点击添加/替换才提交播放器。切换类型或预设会丢弃参数草稿。");
}
fire_and_forget MainWindow::PickFilterLutFile() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    auto input = filterPresetInputs_.find("file");
    if (input == filterPresetInputs_.end()) co_return;
    auto field = input->second.as<Controls::TextBox>();
    auto oldText = field.Text();
    auto epoch = filterPresetEpoch_;
    pickerOpen_ = true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.CommitButtonText(L"选择 LUT 文件");
        // Extension is not a format validator; FFmpeg owns file decoding.
        picker.FileTypeFilter().Append(L"*");
        auto file = co_await picker.PickSingleFileAsync();
        if (file && !closing_ && epoch == filterPresetEpoch_) {
            if (field.Text() != oldText) {
                FilterPresetStatus().Text(L"路径草稿已改变，未覆盖。请重新选择。");
            } else if (file.Path().empty() || to_string(file.Path()).size() > 16384) {
                FilterPresetStatus().Text(L"所选文件没有可用本地路径或路径过长。");
            } else {
                field.Text(file.Path());
                FilterPresetStatus().Text(L"已填入原始路径，尚未生成或添加滤镜。文件格式由FFmpeg确认。");
            }
        }
    } catch (hresult_error const& error) {
        if (!closing_ && epoch == filterPresetEpoch_) FilterPresetStatus().Text(error.message());
    } catch (...) {
        if (!closing_ && epoch == filterPresetEpoch_) FilterPresetStatus().Text(L"无法选择 LUT 文件。");
    }
    pickerOpen_ = false;
    if (!closing_) { OpenPendingFiles(); FlushActivation(); }
}
void MainWindow::GenerateFilterPreset() {
    auto selected = FilterPresetBox().SelectedIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= filterPresets_.size()) return;
    auto const& preset = filterPresets_[selected];
    iima::FilterPresetInstance instance(preset);
    for (auto const& name : preset.parameterOrder) {
        auto const& schema = preset.parameters.at(name);
        auto found = filterPresetInputs_.find(name);
        if (found == filterPresetInputs_.end()) return;
        std::optional<iima::FilterParameterValue> value;
        using Type = iima::FilterParameter::Type;
        switch (schema.type) {
        case Type::Text: {
            auto text = to_string(found->second.as<Controls::TextBox>().Text());
            if (text.size() <= 16384 && text.find('\0') == std::string::npos)
                value.emplace(std::move(text));
            break;
        }
        case Type::Integer: {
            auto number = found->second.as<Controls::NumberBox>().Value();
            if (std::isfinite(number) && std::floor(number) == number &&
                number >= schema.minimumInt && number <= schema.maximumInt)
                value.emplace(static_cast<int>(number));
            break;
        }
        case Type::Float: {
            auto number = found->second.as<Controls::NumberBox>().Value();
            if (std::isfinite(number) && number >= schema.minimum && number <= schema.maximum)
                value.emplace(static_cast<float>(number));
            break;
        }
        case Type::Choice: {
            auto index = found->second.as<Controls::ComboBox>().SelectedIndex();
            if (index >= 0 && static_cast<std::size_t>(index) < schema.choices.size())
                value.emplace(schema.choices[index]);
            break;
        }
        }
        if (!value || !instance.Set(name, *value)) {
            FilterPresetStatus().Text(L"参数无效或不符合步进：" + to_hstring(name)); return;
        }
    }
    auto filter = instance.CreateFilter();
    if (!filter) { FilterPresetStatus().Text(L"无法生成滤镜；自定义滤镜必须提供有效名称。"); return; }
    auto expression = filter->StringFormat();
    if (expression.size() > 65536 || expression.find('\0') != std::string::npos || !iima::MPVFilter::Parse(expression)) {
        FilterPresetStatus().Text(L"生成表达式无效或超过64KiB，未替换手工输入。"); return;
    }
    ChainFilterExpression().Text(to_hstring(expression)); lastGeneratedFilter_ = std::move(expression);
    FilterPresetStatus().Text(FilterTypeBox().SelectedIndex() == 1 ?
        L"已生成音频滤镜表达式，尚未添加。需实际libmpv/FFmpeg支持对应滤镜。" :
        L"已生成视频滤镜表达式，尚未添加。需实际libmpv/FFmpeg支持对应滤镜。");
}
}
