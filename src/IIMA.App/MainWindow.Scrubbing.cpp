// IINA PlaySliderCell.startTracking/stopTracking: pause while adjusting, resume
// only when the same transaction still owns that temporary pause.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireScrubbing() {
    auto weak=get_weak();PositionSlider().SetTrackingEnabled(true);
    PositionSlider().TrackingStarted([weak](auto const&,bool) {if(auto self=weak.get()) self->BeginScrubbing();});
    PositionSlider().TrackingEnded([weak](auto const&,bool cancelled) {if(auto self=weak.get()) self->FinishScrubbing(cancelled);});
}
void MainWindow::BeginScrubbing() {
    if(abMarkerDragging_) {PositionSlider().CancelTracking();return;}
    if(scrubToken_ || scrubStarting_) {PositionSlider().CancelTracking();return;}
    scrubStarting_=true;scrubSuppress_=true;++scrubSuppressEpoch_;
    auto const& state=session_->Snapshot();
    scrubGeneration_=state.mediaGeneration;scrubIntent_=state.mediaOpenIntentRevision;scrubInput_=inputGeneration_;
    scrubDuration_=state.duration.value_or(0);scrubFullscreen_=fullscreen_;scrubMusic_=musicMode_.IsMusic();
    scrubPresenter_=static_cast<std::int64_t>(AppWindow().Presenter().Kind());
    if(!closing_ && userDataLoaded_ && !pickerOpen_ && !oscDragging_ && !interactiveCrop_.Active() && !cropOriginal_ &&
        !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() && PositionSlider().IsEnabled() &&
        state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded && state.seekable &&
        std::isfinite(scrubDuration_) && scrubDuration_>0) scrubToken_=session_->BeginScrub();
    scrubStarting_=false;
    if(!scrubToken_) {CancelScrubbing();return;}
    scrubSuppress_=false;CancelMouseClick();CancelThumbnailPreview();ControlBarActivity();
}
void MainWindow::UpdateScrubbing() {
    if(scrubStarting_ || !scrubToken_) return;
    auto const& state=session_->Snapshot();
    if(session_->ActiveScrubToken()!=scrubToken_ || closing_ || pickerOpen_ || controlBarInactive_ || oscDragging_ ||
        interactiveCrop_.Active() || cropOriginal_ || inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty() ||
        state.session!=iima::SessionState::Ready || state.media!=iima::MediaState::Loaded || !state.seekable ||
        state.mediaGeneration!=scrubGeneration_ || state.mediaOpenIntentRevision!=scrubIntent_ || inputGeneration_!=scrubInput_ ||
        !state.duration || *state.duration!=scrubDuration_ || fullscreen_!=scrubFullscreen_ || musicMode_.IsMusic()!=scrubMusic_ ||
        static_cast<std::int64_t>(AppWindow().Presenter().Kind())!=scrubPresenter_) CancelScrubbing();
}
void MainWindow::CancelScrubbing() {
    const auto token=scrubToken_;scrubToken_=0;scrubStarting_=false;scrubSuppress_=true;
    const auto epoch=++scrubSuppressEpoch_;
    if(token && session_->ActiveScrubToken()==token) session_->CancelScrub();
    PositionSlider().CancelTracking();CancelThumbnailPreview();
    // A rejected/cancelled native pointer dispatch must not fall through to
    // the old absolute ValueChanged seek after tracking ownership disappears.
    auto weak=get_weak();DispatcherQueue().TryEnqueue([weak,epoch] {
        if(auto self=weak.get();self && self->scrubSuppressEpoch_==epoch && !self->scrubToken_ && !self->scrubStarting_) {
            self->scrubSuppress_=false;
            if(!self->closing_) {
                bool before=self->updating_;self->updating_=true;
                self->PositionSlider().Value(self->session_->Snapshot().position.value_or(0));self->updating_=before;
                self->UpdateControlBarPolicy();
            }
        }
    });
}
void MainWindow::FinishScrubbing(bool cancelled) {
    if(cancelled) {if(scrubToken_ || scrubStarting_) CancelScrubbing();return;}
    UpdateScrubbing();if(!scrubToken_) return;
    const auto token=scrubToken_;
    if(!session_->UpdateScrub(token,PositionSlider().Value())) {CancelScrubbing();return;}
    session_->EndScrub(token); // Core retains token until final seek and owned resume settle.
    UpdateScrubbing();UpdateControlBarPolicy();
}
void MainWindow::PositionSliderChanged(double value) {
    if(updating_ || closing_ || abMarkerDragging_ || scrubStarting_ || scrubSuppress_) return;
    if(scrubToken_ && !PositionSlider().Tracking()) {
        // A new keyboard adjustment after pointer-up is a new explicit seek,
        // not another target in the already-ending pointer transaction.
        const auto generation=session_->Snapshot().mediaGeneration,intent=session_->Snapshot().mediaOpenIntentRevision;
        CancelScrubbing();
        if(!closing_ && session_->Snapshot().mediaGeneration==generation && session_->Snapshot().mediaOpenIntentRevision==intent)
            session_->Seek(value);
        return;
    }
    if(scrubToken_) {
        UpdateScrubbing();
        if(scrubToken_ && !session_->UpdateScrub(scrubToken_,value)) CancelScrubbing();
        return;
    }
    session_->Seek(value); // Unchanged keyboard/unsupported-template track-click path.
}
}
