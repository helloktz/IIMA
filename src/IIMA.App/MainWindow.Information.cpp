// IINA InspectorWindowController and audio-device menu behavior adapted to WinUI.
// Copyright © lhc and contributors, GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/MediaInformation.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <set>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
struct RestoreFlag {
    bool& flag;
    bool previous;
    explicit RestoreFlag(bool& value) : flag(value), previous(value) { flag=true; }
    ~RestoreFlag() { flag=previous; }
};
}
namespace winrt::IIMA::implementation {
void MainWindow::WireMediaInformation() {
    auto weak = get_weak();
    MediaInformationExpander().Expanding([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            self->informationRevision_.reset(); self->UpdateMediaInformation(self->session_->Snapshot());
        }
    });
    AudioDeviceBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->updating_)
            if (auto item = self->AudioDeviceBox().SelectedItem().try_as<Controls::ComboBoxItem>()) {
                auto name = to_string(unbox_value<hstring>(item.Tag()));
                if (name != "auto" && !self->session_->Snapshot().audioDevicesObserved) {
                    self->UpdateMediaInformation(self->session_->Snapshot());
                    self->ShowError(L"音频设备列表尚未完整读取，请稍后重新选择。");
                    return;
                }
                self->ApplyAdvancedSetting("audioDevice", std::move(name));
            }
    });
}
void MainWindow::UpdateMediaInformation(iima::PlaybackSnapshot const& state) {
    RestoreFlag projection(updating_);
    try {
    if (audioDevicesRevision_ != state.audioDevicesRevision) {
        auto box = AudioDeviceBox(); box.Items().Clear();
        Controls::ComboBoxItem automatic;
        automatic.Content(box_value(L"系统默认音频输出")); automatic.Tag(box_value(L"auto")); box.Items().Append(automatic);
        std::set<std::string> inserted{"auto"};
        for (auto const& device : state.audioDevices) {
            if (device.name.empty() || !inserted.insert(device.name).second) continue;
            Controls::ComboBoxItem item;
            item.Content(box_value(to_hstring(device.description.empty() ? device.name : device.description)));
            item.Tag(box_value(to_hstring(device.name))); box.Items().Append(item);
        }
        audioDevicesRevision_ = state.audioDevicesRevision;
    }
    // Apply() owns updating_; explicit refresh must not turn state projection
    // into a preference write. Match by backend name, never transient row index.
    int selected = -1;
    for (std::uint32_t index = 0; index < AudioDeviceBox().Items().Size(); ++index) {
        auto item = AudioDeviceBox().Items().GetAt(index).as<Controls::ComboBoxItem>();
        if (state.audioDeviceObserved && to_string(unbox_value<hstring>(item.Tag())) == state.audioDevice)
            selected = static_cast<int>(index);
    }
    AudioDeviceBox().SelectedIndex(selected);
    if (!MediaInformationExpander().IsExpanded() || informationRevision_ == state.informationRevision) return;
    std::string text = "Title: " + iima::MediaDisplayTitle(state.mediaTitle, state.path) + "\n";
    text += "File size: " + iima::FormatByteCount(state.fileSize) + "\n";
    text += "Video bitrate: " + iima::FormatBitrate(state.videoBitrate) + "\n";
    text += "Audio bitrate: " + iima::FormatBitrate(state.audioBitrate) + "\n";
    text += "Video decoder: " + state.videoCodec + "\nHardware decoding: " + state.hwdecCurrent + "\n";
    for (auto const& field : iima::DescribeVideoParameters(state.videoParameters))
        text += field.label + ": " + field.value + "\n";
    text += "\nAudio\n";
    for (auto const& field : iima::DescribeAudioParameters(state.audioParameters))
        text += field.label + ": " + field.value + "\n";
    text += "\nMetadata\n";
    for (auto const& [key,value] : state.metadata) text += key + ": " + value + "\n";
    MediaInformationText().Text(to_hstring(text));
    informationRevision_ = state.informationRevision;
    } catch (...) {
        // Information is auxiliary: conversion/control failures must not break
        // playback state delivery or leave updating_ stuck. No metadata leaks
        // into diagnostic output, and an uncommitted revision will retry.
        OutputDebugStringW(L"IIMA: media information projection failed.\n");
    }
}
}
