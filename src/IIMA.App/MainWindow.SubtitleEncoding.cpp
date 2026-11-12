// IINA MainMenuActions.menuSetSubEncoding / CharEncoding.list.
// Copyright (c) 2016 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Subtitles/CharacterEncoding.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireSubtitleEncoding() {
    for (auto const& encoding : iima::SubtitleEncodings) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        item.Content(box_value(to_hstring(encoding.title)));
        item.Tag(box_value(to_hstring(encoding.code)));
        SubtitleEncodingBox().Items().Append(item);
    }
    SubtitleEncodingBox().SelectedIndex(0);
    auto weak = get_weak();
    ApplySubtitleEncodingButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto item = self->SubtitleEncodingBox().SelectedItem().try_as<Microsoft::UI::Xaml::Controls::ComboBoxItem>();
            if (!item) return;
            if (!self->session_->SetSubtitleEncoding(to_string(unbox_value<hstring>(item.Tag()))))
                self->ShowError(L"媒体未就绪、正在切换或编码操作尚未完成，未提交编码变更。");
        }
    });
}
void MainWindow::UpdateSubtitleEncoding(iima::PlaybackSnapshot const& state) {
    const auto value = state.subtitleStyleValues.find("sub-codepage");
    SubtitleEncodingObservedText().Text(value == state.subtitleStyleValues.end() ?
        L"当前编码：未知" : hstring{L"当前编码：" + std::wstring(to_hstring(value->second))});
    ApplySubtitleEncodingButton().IsEnabled(!closing_ && state.session == iima::SessionState::Ready &&
        state.media == iima::MediaState::Loaded && !session_->SubtitleEncodingBusy());
    // Keep user's candidate separate from observed codepage: periodic time-pos
    // must not overwrite a choice that has not yet been explicitly applied.
}
}
