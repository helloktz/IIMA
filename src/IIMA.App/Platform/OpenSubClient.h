// Windows adaptation of IINA OpenSubClient.swift / OpenSubSubtitle.swift.
// Copyright (c) 2017-2023 lhc and IINA contributors. GPL-3.0.
// See references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace iima::opensub {
struct SearchResult {
    // OpenSubtitles file_id, not the enclosing subtitle ID. Every file in a
    // multi-disc subtitle becomes its own choice; no silent files[0] selection.
    std::int64_t id{};
    std::string title, type, language, uploadTime;
    std::string filename;
    std::optional<double> rating;
    std::optional<bool> hearingImpaired;
    std::int64_t downloadCount{};
};
struct DownloadTicket {
    std::string url, filename;
    std::int64_t remaining{}, requests{};
    std::string resetTime, resetTimeUtc;
};
enum class ErrorCode {
    None, Cancelled, TimedOut, InvalidInput, Connection, Protocol,
    Authentication, RateLimited, QuotaExceeded, NotFound, TooLarge, Unsupported
};
template<class T> struct Result {
    T value{};
    ErrorCode error{ErrorCode::None};
    // Non-secret fixed diagnostic. Server bodies, URLs and credentials are not
    // interpolated into errors. Quota reset time is returned as a separate field.
    std::string message;
    int httpStatus{};
    std::optional<std::int64_t> remaining;
    std::string resetTimeUtc;
    std::int64_t totalPages{1};
    explicit operator bool() const noexcept { return error == ErrorCode::None; }
};
// Worker-only calls with owned MTA and per-request native HTTP client. Do not
// synchronously invoke/join from UI. Cancellation/30-second deadline is checked
// while waiting for native async operations and body reads, not a process kill.
// API key is user-supplied. JWT may be empty for anonymous service access; this
// module neither logs in nor verifies a JWT. Server rejection is authoritative.
// Caller owns credential memory, selection generation and subtitle staging.
// The first result page only is returned, with totalPages describing the server
// response. Maximum 200 subtitle rows / 8 files per row / 400 expanded choices.
Result<std::vector<SearchResult>> Search(std::string const& key,
    std::string const& jwt, std::string const& query, std::string const& language,
    std::stop_token stop = {}, std::string const& movieHash = {});
// This API request consumes a service download allowance even if the later file
// fetch is cancelled or fails. Never retry automatically. No password login or
// server-supplied base_url is accepted, so VIP-host routing is not supported.
Result<DownloadTicket> RequestDownload(std::string const& key,
    std::string const& jwt, std::int64_t fileId, std::stop_token stop = {});
// No API key/JWT/cookies attached. HTTPS opensubtitles.com and subdomains only,
// port 443, no redirects. External CDN hosts are deliberately unsupported.
// Limits: JSON 2 MiB, subtitle 8 MiB. Empty responses are failures.
Result<std::vector<std::uint8_t>> Download(std::string const& url,
    std::stop_token stop = {});
}
