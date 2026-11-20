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
    if (!color) { ShowError(iima::loc::Text(L"ColourMustBeRRGGBBOrAARRGGBBHexadecimal")); return; }
    iima::SubtitleStyle style;
    std::vector<std::string> fields;
    if (kind == 0) { style.color = *color; fields = {"options/sub-color"}; }
    else if (kind == 1) {
        style.outlineColor = *color; style.outlineSize = SubtitleOutlineSizeBox().Value();
        fields = {"options/sub-outline-color", "options/sub-outline-size"};
    } else { style.backgroundColor = *color; fields = {"options/sub-back-color"}; }
    if (!style.Valid()) { ShowError(iima::loc::Text(L"TheEdgeWidthMustBeALimited")); return; }
    session_->ApplySubtitleStyle(style, std::move(fields));
    // Do not claim success or modify observed state; async failures use the
    // session's ordinary error path, and partial changes are not rolled back.
}
void MainWindow::UpdateSubtitleStyleFeedback(iima::PlaybackSnapshot const& state) {
    std::wstring text = iima::loc::Text(L"CurrentKernelValue");
    for (auto const& [name, label] : std::initializer_list<std::pair<char const*, wchar_t const*>>{
        {"sub-font", iima::loc::Text(L"Fonts")}, {"sub-font-size", iima::loc::Text(L"Character")}, {"sub-bold", iima::loc::Text(L"Bold")}, {"sub-italic", iima::loc::Text(L"Italic")},
        {"sub-color", iima::loc::Text(L"TextColour")}, {"sub-outline-color", iima::loc::Text(L"Scene")},
        {"sub-outline-size", iima::loc::Text(L"MarginalWidth")}, {"sub-back-color", iima::loc::Text(L"BackgroundColour")}}) {
        auto found = state.subtitleStyleValues.find(name);
        text += L"\n"; text += label; text += L"：";
        text += found == state.subtitleStyleValues.end() ? iima::loc::Text(L"Unknown3") : to_hstring(found->second).c_str();
    }
    // The observed summary never overwrites an in-progress edit on time-pos.
    hstring rendered{text};
    if (SubtitleStyleObservedText().Text() != rendered) SubtitleStyleObservedText().Text(rendered);
}
}
