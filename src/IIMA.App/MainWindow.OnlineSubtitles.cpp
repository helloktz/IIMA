// IINA OnlineSubtitle._search, Assrt.Fetcher and SubChooseViewController.
// Copyright (c) lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <thread>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireOnlineSubtitles() {
    WireOpenSubAccount();
    auto weak = get_weak();
    OnlineSubtitleProviderBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CancelOnlineSubtitles();
    });
    SaveOnlineSubtitleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SaveOnlineSubtitle();
    });
    commandRouter_.Register(iima::ApplicationCommand::SaveDownloadedSubtitle,
        [weak] { if (auto self = weak.get()) self->SaveOnlineSubtitle(); },
        [weak] { auto self = weak.get(); return self && !self->closing_ && !self->downloadedSubtitles_.empty(); });
    OnlineSubtitleSearchButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->SearchOnlineSubtitles();
    });
    OnlineSubtitleDownloadButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->DownloadOnlineSubtitle();
    });
    OnlineSubtitleCancelButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CancelOnlineSubtitles();
    });
    OnlineSubtitleResultsView().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RefreshOnlineSubtitleButtons();
    });
    OnlineSubtitleUseTitleButton().Click([weak](auto const&, auto const&) {
        auto self = weak.get();
        if (!self || self->closing_) return;
        auto const& state = self->session_->Snapshot();
        // No silent upload of full paths, URLs or embedded credentials. Filling
        // is a separate local action; nothing is sent until Send Query is used.
        const auto path = std::filesystem::path(to_hstring(state.path).c_str());
        if (state.path.find("://") != std::string::npos || !path.is_absolute()) {
            self->OnlineSubtitleStatusText().Text(L"请手动输入片名，不发送网络地址。");
            return;
        }
        self->OnlineSubtitleQueryBox().Text(hstring(path.stem().wstring()));
    });
    commandRouter_.Register(iima::ApplicationCommand::FindOnlineSubtitles, [weak] {
        if (auto self = weak.get()) {
            self->OnlineSubtitleExpander().IsExpanded(true);
            self->OnlineSubtitleQueryBox().Focus(FocusState::Programmatic);
        }
    }, [weak] { auto self = weak.get(); return self && !self->closing_; });
    RefreshOnlineSubtitleButtons();
}
void MainWindow::RefreshOnlineSubtitleButtons() {
    ExpireOpenSubAccount();
    const bool loaded = !closing_ && session_ && session_->Snapshot().media == iima::MediaState::Loaded;
    OnlineSubtitleSearchButton().IsEnabled(loaded && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_);
    OnlineSubtitleUseTitleButton().IsEnabled(loaded && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_);
    OnlineSubtitleQueryBox().IsEnabled(!onlineSubtitleWorkerActive_ && !openSubAuthBusy_ && !closing_);
    OnlineSubtitleTokenBox().IsEnabled(OnlineSubtitleProviderBox().SelectedIndex() != 2 && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_ && !closing_);
    OnlineSubtitleProviderBox().IsEnabled(!onlineSubtitleWorkerActive_ && !openSubAuthBusy_ && !closing_);
    const bool openSub = OnlineSubtitleProviderBox().SelectedIndex() == 1;
    OnlineSubtitleApiKeyBox().IsEnabled(openSub && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_ && !closing_);
    OnlineSubtitleLanguageBox().IsEnabled(openSub && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_ && !closing_);
    OnlineSubtitleHashCheck().IsEnabled(openSub && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_ && !closing_);
    OpenSubLoginButton().IsEnabled(openSub && !closing_ && !openSubAuthBusy_ && !onlineSubtitleWorkerActive_ && openSubSessionToken_.empty());
    OpenSubLogoutButton().IsEnabled(openSub && !closing_ && !openSubAuthBusy_ && !onlineSubtitleWorkerActive_ && !openSubSessionToken_.empty());
    OpenSubCancelLoginButton().IsEnabled(!closing_ && openSubAuthBusy_);
    OpenSubUsernameBox().IsEnabled(openSub && !openSubAuthBusy_ && !closing_);
    OpenSubPasswordBox().IsEnabled(openSub && !openSubAuthBusy_ && !closing_);
    SaveOnlineSubtitleButton().IsEnabled(!closing_ && !downloadedSubtitles_.empty());
    const auto selected = OnlineSubtitleResultsView().SelectedIndex();
    OnlineSubtitleDownloadButton().IsEnabled(loaded && !onlineSubtitleWorkerActive_ && !openSubAuthBusy_ &&
        onlineSubtitleTransaction_.State() == iima::SubtitleSearchTransaction::Phase::Choosing &&
        selected >= 0 && static_cast<std::size_t>(selected) < onlineSubtitleResults_.size());
}
void MainWindow::CancelOnlineSubtitles() {
    CancelOpenSubAccountPending();
    onlineSubtitleStop_.request_stop();
    onlineSubtitleTransaction_.Cancel();
    onlineSubtitleResults_.clear(); onlineShooterResults_.clear();
    onlineSubtitleToken_.clear();
    onlineSubtitleApiKey_.clear();
    OnlineSubtitleApiKeyBox().Password(L"");
    OnlineSubtitleTokenBox().Password(L"");
    OnlineSubtitleResultsView().Items().Clear();
    OnlineSubtitleStatusText().Text(L"已取消。已发送的网络请求无法撤回。");
    // A cancelled request must actually return before another can begin. This
    // bounds workers even if a native network operation is slow to cancel.
    RefreshOnlineSubtitleButtons();
}
void MainWindow::UpdateOnlineSubtitleMedia(iima::PlaybackSnapshot const& state) {
    if (auto ticket = onlineSubtitleTransaction_.Current(); ticket &&
        (state.media != iima::MediaState::Loaded ||
         !onlineSubtitleTransaction_.MatchesMedia({state.entryId, state.path}))) {
        CancelOnlineSubtitles();
    }
    RefreshOnlineSubtitleButtons();
}
void MainWindow::SearchOnlineSubtitles() {
    if (closing_ || openSubAuthBusy_ || onlineSubtitleWorkerActive_ || session_->Snapshot().media != iima::MediaState::Loaded) return;
    auto token = to_string(OnlineSubtitleTokenBox().Password());
    auto query = to_string(OnlineSubtitleQueryBox().Text());
    const bool openSub = OnlineSubtitleProviderBox().SelectedIndex() == 1;
    auto apiKey = to_string(OnlineSubtitleApiKeyBox().Password());
    ExpireOpenSubAccount();
    const bool usingSession = openSub && token.empty() && apiKey.empty() && !openSubSessionToken_.empty();
    if (usingSession) {
        token=openSubSessionToken_; apiKey=openSubSessionKey_;
    }
    auto language = to_string(OnlineSubtitleLanguageBox().Text());
    const bool shooter = OnlineSubtitleProviderBox().SelectedIndex() == 2;
    const bool useHash = openSub && OnlineSubtitleHashCheck().IsChecked().Value();
    if ((!openSub && !shooter && token.size() != 32) || (openSub && apiKey.empty()) || query.empty() || query.size() > 1024) {
        OnlineSubtitleStatusText().Text(L"请输入搜索词，以及服务所需的个人令牌或 API Key。"); return;
    }
    // Do not accidentally send a file path/URL when the user copied one into
    // the title field. The provider only needs a title, not a local location.
    if (query.find("://") != std::string::npos || query.find('\\') != std::string::npos ||
        query.starts_with('/') || (query.size() > 1 && query[1] == ':') || query.find('\0') != std::string::npos) {
        OnlineSubtitleStatusText().Text(L"请只输入片名，不输入完整路径或网址。"); return;
    }
    onlineSubtitleStop_.request_stop();
    onlineSubtitleStop_ = std::stop_source{};
    const auto stop = onlineSubtitleStop_.get_token();
    const auto& state = session_->Snapshot();
    const auto ticket = onlineSubtitleTransaction_.Search({state.entryId, state.path});
    onlineSubtitleResults_.clear(); onlineShooterResults_.clear();
    OnlineSubtitleResultsView().Items().Clear();
    onlineSubtitleSessionCredentials_ = usingSession;
    onlineSubtitleProvider_ = shooter ? 2 : openSub ? 1 : 0;
    if (shooter) { token.clear(); apiKey.clear(); }
    onlineSubtitleToken_ = token;
    onlineSubtitleApiKey_ = apiKey;
    OnlineSubtitleApiKeyBox().Password(L"");
    OnlineSubtitleTokenBox().Password(L"");
    onlineSubtitleWorkerActive_ = true;
    OnlineSubtitleStatusText().Text(L"正在搜索在线字幕…");
    RefreshOnlineSubtitleButtons();
    auto weak = get_weak();
    auto queue = DispatcherQueue();
    try {
        std::thread([weak, queue, stop, ticket, openSub, shooter, useHash, language = std::move(language),
            apiKey = std::move(apiKey), token = std::move(token), query = std::move(query)] {
            iima::assrt::Result<std::vector<iima::assrt::SearchResult>> result;
            std::int64_t totalPages = 1;
            std::vector<iima::shooter::SearchResult> shooterResults;
            if (shooter) {
                try {
                    auto hash = iima::ReadShooterMovieHash(
                        std::filesystem::path(to_hstring(ticket.media.path).c_str()),stop);
                    auto response = iima::shooter::Search(hash.hash,query,stop);
                    if (!response) { result.error = response.error; result.message = response.message; }
                    else {
                        shooterResults = std::move(response.value);
                        for (auto const& row : shooterResults) result.value.push_back(row.row);
                    }
                } catch (...) {
                    result.error = iima::assrt::ErrorCode::InvalidInput;
                    result.message = "Shooter requires a stable readable local movie of at least 12 KiB.";
                }
            } else if (!openSub) result = iima::assrt::Search(token, query, stop);
            else try {
                std::string hash;
                if (useHash) hash = iima::ReadOpenSubMovieHash(
                    std::filesystem::path(to_hstring(ticket.media.path).c_str()), stop).hash;
                auto response = iima::opensub::Search(apiKey, token, query, language, stop, hash);
                totalPages = response.totalPages;
                if (!response) { result.error = response.error == iima::opensub::ErrorCode::Authentication ? iima::assrt::ErrorCode::Authentication : iima::assrt::ErrorCode::Protocol; result.message = response.message; }
                else for (auto const& row : response.value) {
                    const auto title = row.title == row.filename ? row.title : row.title + " — " + row.filename;
                    result.value.push_back({row.id, title, row.type, row.language, row.uploadTime});
                }
            } catch (...) {
                result.error = iima::assrt::ErrorCode::InvalidInput;
                result.message = "Could not read a stable local movie hash. Disable hash matching to search by title.";
            }
            try { queue.TryEnqueue([weak, stop, ticket, totalPages, shooterResults = std::move(shooterResults), result = std::move(result)]() mutable {
                auto self = weak.get();
                if (!self || self->closing_) return;
                self->onlineSubtitleWorkerActive_ = false;
                if (stop.stop_requested() || !self->onlineSubtitleTransaction_.Matches(ticket)) {
                    self->RefreshOnlineSubtitleButtons(); return;
                }
                if (!result) {
                    if (result.error == iima::assrt::ErrorCode::Authentication && self->onlineSubtitleSessionCredentials_) {
                        self->openSubSessionToken_.clear(); self->openSubSessionKey_.clear();
                        self->OpenSubAccountStatus().Text(L"服务器拒绝当前登录凭据，已清除本地会话；请检查API Key并重新登录。");
                    }
                    self->onlineSubtitleTransaction_.Fail(ticket);
                    self->onlineSubtitleToken_.clear(); self->onlineSubtitleApiKey_.clear();
                    self->OnlineSubtitleStatusText().Text(to_hstring(result.message));
                } else if (self->onlineSubtitleTransaction_.SearchFinished(ticket, !result.value.empty())) {
                    self->onlineSubtitleResults_ = std::move(result.value);
                    self->onlineShooterResults_ = std::move(shooterResults);
                    for (auto const& row : self->onlineSubtitleResults_) {
                        Controls::TextBlock text;
                        text.Text(to_hstring(row.title + "\n" + row.type + "  " + row.language + "  " + row.uploadTime));
                        text.TextWrapping(TextWrapping::Wrap);
                        self->OnlineSubtitleResultsView().Items().Append(text);
                    }
                    if (self->onlineSubtitleResults_.empty()) {
                        self->onlineSubtitleToken_.clear(); self->onlineSubtitleApiKey_.clear();
                        self->OnlineSubtitleStatusText().Text(self->onlineSubtitleProvider_ == 2
                            ? L"未找到受支持的HTTPS文本字幕；服务可能返回了不支持的格式或下载域名。" : L"未找到字幕。");
                    } else {
                        self->OnlineSubtitleStatusText().Text(totalPages > 1 ? L"已显示第一页，请缩小搜索词以找到目标。选择结果后下载文本字幕。" : L"请选择一个结果，再下载。只支持文本字幕，不解压归档。");
                        self->OnlineSubtitleResultsView().SelectedIndex(0);
                    }
                }
                self->RefreshOnlineSubtitleButtons();
            }); } catch (...) { /* Window dispatcher closed; no UI ownership remains. */ }
        }).detach();
    } catch (...) {
        onlineSubtitleWorkerActive_ = false;
        onlineSubtitleTransaction_.Fail(ticket);
        onlineSubtitleToken_.clear(); onlineSubtitleApiKey_.clear();
        OnlineSubtitleStatusText().Text(L"无法创建字幕搜索任务。");
        RefreshOnlineSubtitleButtons();
    }
}
void MainWindow::DownloadOnlineSubtitle() {
    if (closing_ || openSubAuthBusy_ || onlineSubtitleWorkerActive_) return;
    auto selected = OnlineSubtitleResultsView().SelectedIndex();
    auto ticket = onlineSubtitleTransaction_.Current();
    if (!ticket || selected < 0 || static_cast<std::size_t>(selected) >= onlineSubtitleResults_.size()) return;
    auto const& media = session_->Snapshot();
    if (media.media != iima::MediaState::Loaded ||
        !onlineSubtitleTransaction_.MatchesMedia({media.entryId, media.path})) { CancelOnlineSubtitles(); return; }
    constexpr std::size_t sessionBudget = 64 * 1024 * 1024;
    if (onlineSubtitleFiles_.size() >= 8 || onlineSubtitleBytes_ >= sessionBudget) {
        OnlineSubtitleStatusText().Text(L"此窗口已达到临时字幕限额，请关闭窗口后重试。"); return;
    }
    if (!onlineSubtitleTransaction_.Download(*ticket)) return;
    onlineSubtitleStop_ = std::stop_source{};
    auto stop = onlineSubtitleStop_.get_token();
    auto token = std::move(onlineSubtitleToken_);
    auto apiKey = std::move(onlineSubtitleApiKey_);
    const bool openSub = onlineSubtitleProvider_ == 1;
    const bool shooter = onlineSubtitleProvider_ == 2;
    std::optional<iima::assrt::DownloadFile> shooterFile;
    if (shooter) {
        auto id = onlineSubtitleResults_[static_cast<std::size_t>(selected)].id;
        auto found = std::find_if(onlineShooterResults_.begin(),onlineShooterResults_.end(),
            [id](auto const& row) { return row.row.id == id; });
        if (found == onlineShooterResults_.end()) {
            onlineSubtitleTransaction_.Fail(*ticket);
            OnlineSubtitleStatusText().Text(L"Shooter选择已失效，请重新搜索。"); RefreshOnlineSubtitleButtons(); return;
        }
        shooterFile = found->file;
    }
    auto id = onlineSubtitleResults_[static_cast<std::size_t>(selected)].id;
    auto budget = sessionBudget - onlineSubtitleBytes_;
    auto weak = get_weak();
    auto queue = DispatcherQueue();
    onlineSubtitleWorkerActive_ = true;
    OnlineSubtitleStatusText().Text(L"正在下载字幕…");
    RefreshOnlineSubtitleButtons();
    try {
        std::thread([weak, queue, stop, ticket = *ticket, openSub, shooter, shooterFile = std::move(shooterFile), apiKey = std::move(apiKey), token = std::move(token), id, budget] {
            struct Completed {
                std::shared_ptr<iima::SubtitleStaging> staging;
                std::vector<std::filesystem::path> files;
                std::size_t bytes{};
                std::string error;
                std::string quota;
                bool authenticationRejected{};
            } completed;
            try {
                iima::assrt::Result<std::vector<iima::assrt::DownloadFile>> details;
                if (shooter && shooterFile) details.value.push_back(*shooterFile);
                else if (!openSub) details = iima::assrt::Details(token, id, stop);
                else {
                    auto response = iima::opensub::RequestDownload(apiKey, token, id, stop);
                    if (!response) {
                        completed.authenticationRejected = response.error == iima::opensub::ErrorCode::Authentication;
                        details.error = iima::assrt::ErrorCode::Protocol;
                        details.message = response.message;
                        if (response.remaining) details.message += " Remaining: " + std::to_string(*response.remaining);
                        if (!response.resetTimeUtc.empty()) details.message += " Reset: " + response.resetTimeUtc;
                    } else {
                        details.value.push_back({response.value.filename, response.value.url});
                        completed.quota = " Remaining: " + std::to_string(response.value.remaining);
                        if (!response.value.resetTimeUtc.empty()) completed.quota += " Reset: " + response.value.resetTimeUtc;
                    }
                }
                if (!details) completed.error = details.message;
                else {
                    completed.staging = std::make_shared<iima::SubtitleStaging>(iima::SubtitleStaging::Create());
                    for (auto const& file : details.value) {
                        if (stop.stop_requested()) break;
                        iima::assrt::Result<std::vector<std::uint8_t>> data;
                        if (shooter) data = iima::shooter::Download(file.url, stop);
                        else if (!openSub) data = iima::assrt::Download(file.url, stop);
                        else {
                            auto response = iima::opensub::Download(file.url, stop);
                            data.value = std::move(response.value);
                            if (!response) { data.error = iima::assrt::ErrorCode::Protocol; data.message = response.message; }
                        }
                        if (!data) { completed.error = data.message; break; }
                        if (data.value.size() > budget - completed.bytes) {
                            completed.error = "Downloaded subtitles exceed this window's temporary-file budget."; break;
                        }
                        completed.files.push_back(completed.staging->Write(file.filename, data.value));
                        completed.bytes += data.value.size();
                    }
                    if (!stop.stop_requested() && completed.files.empty() && completed.error.empty())
                        completed.error = "The provider returned no supported subtitle files.";
                }
            } catch (...) { completed.error = "Could not store downloaded subtitle files."; }
            // All-or-nothing staging: a failed multi-file result never partially
            // changes the player's track list; downloads are not executed content.
            if (stop.stop_requested() || !completed.error.empty()) {
                completed.files.clear(); completed.staging.reset(); completed.bytes = 0;
            }
            try { queue.TryEnqueue([weak, stop, ticket, completed = std::move(completed)]() mutable {
                auto self = weak.get();
                if (!self || self->closing_) return;
                self->onlineSubtitleWorkerActive_ = false;
                if (stop.stop_requested() || !self->onlineSubtitleTransaction_.Matches(ticket)) {
                    self->RefreshOnlineSubtitleButtons(); return;
                }
                auto const& current = self->session_->Snapshot();
                if (current.media != iima::MediaState::Loaded ||
                    !self->onlineSubtitleTransaction_.MatchesMedia({current.entryId, current.path})) {
                    self->CancelOnlineSubtitles(); return;
                }
                if (!completed.error.empty()) {
                    if (completed.authenticationRejected && self->onlineSubtitleSessionCredentials_) {
                        self->openSubSessionToken_.clear(); self->openSubSessionKey_.clear();
                        self->OpenSubAccountStatus().Text(L"服务器拒绝当前登录凭据，已清除本地会话；请重新登录。");
                    }
                    self->onlineSubtitleTransaction_.Fail(ticket);
                    self->OnlineSubtitleStatusText().Text(to_hstring(completed.error));
                } else if (self->onlineSubtitleTransaction_.DownloadFinished(ticket)) {
                    // Retain the actual files until mpv termination, not just
                    // until sub-add returns. It may reopen them on track changes.
                    self->onlineSubtitleFiles_.push_back(completed.staging);
                    self->onlineSubtitleBytes_ += completed.bytes;
                    bool first = true;
                    for (auto const& path : completed.files) {
                        self->downloadedSubtitles_.push_back({path, completed.staging});
                        self->DownloadedSubtitleBox().Items().Append(box_value(hstring(path.filename().wstring())));
                        self->session_->AddSubtitle(to_string(path.native()), first);
                        first = false;
                    }
                    self->DownloadedSubtitleBox().SelectedIndex(static_cast<int>(self->downloadedSubtitles_.size() - completed.files.size()));
                    self->OnlineSubtitleStatusText().Text(L"下载完成，已向播放器提交字幕。加载错误会在播放器状态栏显示。" + to_hstring(completed.quota));
                }
                self->RefreshOnlineSubtitleButtons();
            }); } catch (...) { /* Closing dispatcher releases the staged files. */ }
        }).detach();
    } catch (...) {
        onlineSubtitleWorkerActive_ = false;
        onlineSubtitleTransaction_.Fail(*ticket);
        OnlineSubtitleStatusText().Text(L"无法创建字幕下载任务。请重新搜索。");
        RefreshOnlineSubtitleButtons();
    }
}
}
