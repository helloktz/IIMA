// IINA OpenSubClient.login/logout and tokenExpiration Windows UI adaptation.
// Copyright (c) 2017-2023 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <thread>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireOpenSubAccount() {
    auto weak=get_weak();
    OpenSubLoginButton().Click([weak](auto const&,auto const&) {
        if (auto self=weak.get()) self->BeginOpenSubAccount(false);
    });
    OpenSubLogoutButton().Click([weak](auto const&,auto const&) {
        if (auto self=weak.get()) self->BeginOpenSubAccount(true);
    });
    OpenSubCancelLoginButton().Click([weak](auto const&,auto const&) {
        if (auto self=weak.get()) {
            self->CancelOpenSubAccountPending();
            self->OpenSubAccountStatus().Text(iima::loc::Text(L"TheResultOfTheRequestToReceive"));
            self->RefreshOnlineSubtitleButtons();
        }
    });
}
void MainWindow::CancelOpenSubAccountPending() {
    openSubAuthStop_.request_stop(); ++openSubAuthGeneration_;
    OpenSubPasswordBox().Password(L"");
    // Keep worker busy until it returns; cancelling never starts a second one.
}
void MainWindow::ExpireOpenSubAccount() {
    if (!openSubSessionToken_.empty() && std::chrono::steady_clock::now() >= openSubSessionExpiry_) {
        openSubSessionToken_.clear(); openSubSessionKey_.clear();
        OpenSubAccountStatus().Text(iima::loc::Text(L"ThisWindowHasBeenLoggedInFor"));
    }
}
void MainWindow::BeginOpenSubAccount(bool logout) {
    if (closing_ || onlineSubtitleWorkerActive_ || openSubAuthBusy_ ||
        OnlineSubtitleProviderBox().SelectedIndex()!=1) return;
    ExpireOpenSubAccount();
    std::string key,username,password,token;
    if (logout) {
        if (openSubSessionToken_.empty()) return;
        key=std::move(openSubSessionKey_); token=std::move(openSubSessionToken_);
        openSubSessionKey_.clear(); openSubSessionToken_.clear();
    } else {
        if (!openSubSessionToken_.empty()) return;
        key=to_string(OnlineSubtitleApiKeyBox().Password());
        username=to_string(OpenSubUsernameBox().Text());
        password=to_string(OpenSubPasswordBox().Password());
        if (key.empty() || username.empty() || password.empty()) {
            OpenSubAccountStatus().Text(iima::loc::Text(L"PleaseFillInTheUsernameAndPassword")); return;
        }
    }
    CancelOnlineSubtitles(); // Invalidate old search results and credential copies.
    OnlineSubtitleApiKeyBox().Password(L"");
    OnlineSubtitleTokenBox().Password(L""); OpenSubPasswordBox().Password(L"");
    openSubAuthStop_=std::stop_source{};
    auto stop=openSubAuthStop_.get_token();
    const auto generation=++openSubAuthGeneration_;
    openSubAuthBusy_=true;
    OpenSubAccountStatus().Text(logout ? iima::loc::Text(L"LocalSessionClearedRequestingServerExit") : iima::loc::Text(L"SubmitLoginToOpenSubtitlesPasswordNotSaved"));
    RefreshOnlineSubtitleButtons();
    auto weak=get_weak(); auto queue=DispatcherQueue();
    try {
        std::thread([weak,queue,stop,generation,logout,key=std::move(key),username=std::move(username),
            password=std::move(password),token=std::move(token)]() mutable {
            iima::opensub::Result<iima::opensub::LoginTicket> result;
            if (logout) {
                auto response=iima::opensub::Logout(key,token,stop);
                if (!response) { result.error=response.error; result.message=response.message; }
            } else result=iima::opensub::Login(key,username,password,stop);
            password.clear(); username.clear(); token.clear();
            try { queue.TryEnqueue([weak,stop,generation,logout,key=std::move(key),result=std::move(result)]() mutable {
                auto self=weak.get(); if (!self || self->closing_) return;
                self->openSubAuthBusy_=false;
                if (stop.stop_requested() || generation!=self->openSubAuthGeneration_) {
                    self->RefreshOnlineSubtitleButtons(); return;
                }
                if (!result) {
                    self->OpenSubAccountStatus().Text((logout ? iima::loc::Text(L"LocalSessionClearedButTheServerExits") : iima::loc::Text(L"CannotInitialiseEvolutionSMailComponent"))+to_hstring(result.message));
                } else if (logout) self->OpenSubAccountStatus().Text(iima::loc::Text(L"LocalSessionClearedServerConfirmedExit"));
                else {
                    self->openSubSessionKey_=std::move(key);
                    self->openSubSessionToken_=std::move(result.value.token);
                    self->openSubSessionExpiry_=std::chrono::steady_clock::now()+std::chrono::hours(23);
                    self->OpenSubAccountStatus().Text(iima::loc::Text(L"LoginOnlyTheAPIKeyAndTokens"));
                }
                self->RefreshOnlineSubtitleButtons();
            }); } catch (...) { /* Closing dispatcher drops owned secrets. */ }
        }).detach();
    } catch (...) {
        openSubAuthBusy_=false;
        OpenSubAccountStatus().Text(logout ? iima::loc::Text(L"LocalSessionHasBeenClearedAndThe") : iima::loc::Text(L"CouldNotStartSignInNoSession"));
        RefreshOnlineSubtitleButtons();
    }
}
}
