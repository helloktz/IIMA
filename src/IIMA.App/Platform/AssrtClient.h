// Windows adaptation of IINA AssrtSubtitle.swift / OnlineSubtitle.swift.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0.
// See references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

namespace iima::assrt {
struct SearchResult {
    std::int64_t id{};
    std::string title;
    std::string type;
    std::string language;
    std::string uploadTime;
};
struct DownloadFile {
    // A single Windows-safe leaf, never a directory or an archive path.
    std::string filename;
    std::string url;
};
enum class ErrorCode {
    None, Cancelled, TimedOut, InvalidInput, Connection, Protocol,
    Authentication, RateLimited, NotFound, TooLarge, Unsupported
};
template<class T> struct Result {
    T value{};
    ErrorCode error{ErrorCode::None};
    // Fixed, non-secret text; never includes URLs, query, token or server body.
    std::string message;
    int providerStatus{};
    explicit operator bool() const noexcept { return error == ErrorCode::None; }
};

// Background-thread-only entry points. The worker must not be an STA/UI thread.
// Each call owns its MTA initialization, HTTP filter/client and cancellation.
// Never invoke or join these functions synchronously from a WinUI callback.
// Each request has a 30-second wall-clock budget and cooperative cancellation;
// timeout/cancel calls Cancel on the pending native operation and closes client.
// The caller must separately check media/request generation before applying data.
// Token is caller-supplied, not persisted by this client, and sent only in the
// Authorization header to the fixed api.assrt.net endpoint. No shared IINA key.
Result<std::vector<SearchResult>> Search(std::string const& token,
    std::string const& query, std::stop_token stop = {});
Result<std::vector<DownloadFile>> Details(std::string const& token,
    std::int64_t id, std::stop_token stop = {});
// Download does not accept or send a bearer token. HTTPS assrt.net hosts only,
// no redirect following, cookies, automatic credentials or archive extraction.
// JSON is bounded to 2 MiB, downloaded subtitle bodies to 8 MiB. Caller chooses
// a private unique staging directory and must not trust this leaf as a full path.
Result<std::vector<std::uint8_t>> Download(std::string const& url,
    std::stop_token stop = {});
}
