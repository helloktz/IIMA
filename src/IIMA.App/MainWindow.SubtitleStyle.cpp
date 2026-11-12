// IINA SidebarSubtitlesPane.SubStyleView.colorAction / PlayerCore.setSubText*.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <initializer_list>
#include <utility>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::ApplySubtitleColorDraft(unsigned kind) {
    if (closing_ || session_->Snapshot().media != iima::MediaState::Loaded || kind > 2) return;
    auto text = kind == 0 ? SubtitleColorText().Text() :
                kind == 1 ? SubtitleOutlineColorText().Text() : SubtitleBackgroundText().Text();
    auto color = iima::SubtitleColor::ParseHex(to_string(text));
    if (!color) { ShowError(L"颜色必须是 #RRGGBB 或 #AARRGGBB 十六进制值。"); return; }
    iima::SubtitleStyle style;
    std::vector<std::string> fields;
    if (kind == 0) { style.color = *color; fields = {"options/sub-color"}; }
    else if (kind == 1) {
        style.outlineColor = *color; style.outlineSize = SubtitleOutlineSizeBox().Value();
        fields = {"options/sub-outline-color", "options/sub-outline-size"};
    } else { style.backgroundColor = *color; fields = {"options/sub-back-color"}; }
    if (!style.Valid()) { ShowError(L"描边宽度必须是0到100之间的有限值。"); return; }
    session_->ApplySubtitleStyle(style, std::move(fields));
    // Do not claim success or modify observed state; async failures use the
    // session's ordinary error path, and partial changes are not rolled back.
}
void MainWindow::UpdateSubtitleStyleFeedback(iima::PlaybackSnapshot const& state) {
    std::wstring text = L"当前内核值：";
    for (auto const& [name, label] : std::initializer_list<std::pair<char const*, wchar_t const*>>{
        {"sub-font", L"字体"}, {"sub-font-size", L"字号"}, {"sub-bold", L"粗体"}, {"sub-italic", L"斜体"},
        {"sub-color", L"文字色"}, {"sub-outline-color", L"描边色"},
        {"sub-outline-size", L"描边宽度"}, {"sub-back-color", L"背景色"}}) {
        auto found = state.subtitleStyleValues.find(name);
        text += L"\n"; text += label; text += L"：";
        text += found == state.subtitleStyleValues.end() ? L"未知" : to_hstring(found->second).c_str();
    }
    // The observed summary never overwrites an in-progress edit on time-pos.
    hstring rendered{text};
    if (SubtitleStyleObservedText().Text() != rendered) SubtitleStyleObservedText().Text(rendered);
}
}
