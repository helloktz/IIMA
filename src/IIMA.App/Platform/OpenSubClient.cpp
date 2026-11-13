// Windows adaptation of IINA OpenSubClient.swift / OpenSubSubtitle.swift.
// Copyright (c) 2017-2023 lhc and IINA contributors. GPL-3.0.
#include "pch.h"
#include "OpenSubClient.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <iterator>
#include <new>
#include <string_view>
#include <thread>
#include <utility>
#include <charconv>
#include <mutex>
#include <set>
#include <optional>

namespace iima::opensub {
namespace {
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Filters;
using namespace winrt::Windows::Data::Json;
using Clock = std::chrono::steady_clock;
constexpr std::size_t JsonLimit = 2 * 1024 * 1024;
constexpr std::size_t SubtitleLimit = 8 * 1024 * 1024;
constexpr std::size_t MaximumResults = 200;
constexpr std::size_t MaximumFiles = 8;
struct Failure {
    ErrorCode code;
    char const* message;
    int status{};
    std::optional<std::int64_t> remaining;
    std::string resetTimeUtc;
};
[[noreturn]] void Fail(ErrorCode code, char const* message, int status = 0) {
    throw Failure{code, message, status, {}, {}};
}
struct Apartment {
    Apartment() { winrt::check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~Apartment() { CoUninitialize(); }
};
void Check(std::stop_token stop, Clock::time_point deadline) {
    if (stop.stop_requested()) Fail(ErrorCode::Cancelled, "Subtitle request cancelled.");
    if (Clock::now() >= deadline) Fail(ErrorCode::TimedOut, "Subtitle request timed out.");
}
// Polling is confined to the dedicated background worker, never the UI. Captures
// no worker-owned callback state in native async completion, so returning after
// Cancel cannot leave a dangling callback into a destroyed stack frame.
template<class Async> auto Await(Async const& operation, std::stop_token stop,
    Clock::time_point deadline) {
    try {
        while (operation.Status() == AsyncStatus::Started) {
            Check(stop, deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        Check(stop, deadline);
        return operation.GetResults();
    } catch (...) {
        try { operation.Cancel(); } catch (...) {}
        throw;
    }
}
std::string LowerAscii(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
void ValidateCredentials(std::string const& key, std::string const& jwt) {
    auto visible = [](unsigned char c) { return c >= 33 && c <= 126; };
    if (key.empty() || key.size() > 256 || !std::all_of(key.begin(), key.end(), visible))
        Fail(ErrorCode::InvalidInput, "Enter a valid personal OpenSubtitles API key.");
    if (jwt.size() > 8192 || !std::all_of(jwt.begin(), jwt.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '=';
    })) Fail(ErrorCode::InvalidInput, "Enter a valid OpenSubtitles user token.");
}
Uri DownloadUri(std::string const& value) {
    if (value.empty() || value.size() > 8192 || value.find('\0') != std::string::npos)
        Fail(ErrorCode::Unsupported, "Subtitle download address is not supported.");
    Uri uri{winrt::to_hstring(value)};
    auto host = LowerAscii(winrt::to_string(uri.Host()));
    if (uri.SchemeName() != L"https" || uri.Port() != 443 ||
        !uri.UserName().empty() || !uri.Password().empty() || !uri.Fragment().empty() ||
        !(host == "opensubtitles.com" || host.ends_with(".opensubtitles.com")))
        Fail(ErrorCode::Unsupported, "Only HTTPS OpenSubtitles subtitle downloads are supported.");
    return uri;
}
struct Client {
    HttpBaseProtocolFilter filter;
    HttpClient http{nullptr};
    Client() {
        filter.AllowAutoRedirect(false);
        filter.AllowUI(false);
        filter.CookieUsageBehavior(HttpCookieUsageBehavior::NoCookies);
        filter.ServerCredential(nullptr);
        filter.ProxyCredential(nullptr);
        filter.CacheControl().ReadBehavior(HttpCacheReadBehavior::NoCache);
        filter.CacheControl().WriteBehavior(HttpCacheWriteBehavior::NoCache);
        http = HttpClient(filter);
    }
    ~Client() {
        try { http.Close(); } catch (...) {}
        try { filter.Close(); } catch (...) {}
    }
};
struct RawResponse { int status{}; std::vector<std::uint8_t> bytes; };
// App-wide conservative window, no keys/JWTs stored. Serialize API calls to
// avoid overwriting newer rate-limit state with an older concurrent response.
std::timed_mutex ApiMutex;
std::int64_t ApiRemaining{-1};
Clock::time_point ApiReset{};
std::optional<std::int64_t> UnsignedHeader(HttpResponseMessage const& response, wchar_t const* name) {
    auto headers = response.Headers();
    if (!headers.HasKey(name)) return {};
    auto text = winrt::to_string(headers.Lookup(name));
    std::int64_t number{};
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number < 0) return {};
    return number;
}
RawResponse Request(Uri const& uri, std::string const& key, std::string const& jwt,
    winrt::hstring const& body, bool post, std::size_t maximum, std::stop_token stop, bool remove = false) {
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    Check(stop, deadline);
    const bool api = !key.empty();
    std::unique_lock<std::timed_mutex> lock(ApiMutex, std::defer_lock);
    if (api) {
        while (!lock.try_lock_for(std::chrono::milliseconds(15))) Check(stop, deadline);
        if (ApiReset > Clock::now() && ApiRemaining == 0)
            Fail(ErrorCode::RateLimited, "OpenSubtitles API rate limit is active. Try again later.");
        if (ApiReset > Clock::now() && ApiRemaining > 0) --ApiRemaining;
        if (uri.Host() != L"api.opensubtitles.com" || uri.SchemeName() != L"https" || uri.Port() != 443)
            Fail(ErrorCode::InvalidInput, "Invalid subtitle API destination.");
    }
    Client client;
    HttpRequestMessage request(remove ? HttpMethod::Delete() : post ? HttpMethod::Post() : HttpMethod::Get(), uri);
    request.Headers().TryAppendWithoutValidation(L"User-Agent", L"IIMA");
    request.Headers().TryAppendWithoutValidation(L"Accept", api ? L"application/json" : L"*/*");
    if (api) {
        request.Headers().TryAppendWithoutValidation(L"Api-Key", winrt::to_hstring(key));
        if (!jwt.empty()) request.Headers().Authorization(
            winrt::Windows::Web::Http::Headers::HttpCredentialsHeaderValue(L"Bearer", winrt::to_hstring(jwt)));
    }
    if (post) request.Content(HttpStringContent(body, UnicodeEncoding::Utf8, L"application/json"));
    auto response = Await(client.http.SendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead), stop, deadline);
    const auto status = static_cast<int>(response.StatusCode());
    if (api) {
        auto remaining = UnsignedHeader(response, L"ratelimit-remaining");
        auto reset = UnsignedHeader(response, L"ratelimit-reset");
        if (remaining && reset && *reset <= 86400) {
            ApiRemaining = *remaining;
            ApiReset = Clock::now() + std::chrono::seconds(*reset);
        }
        if (status == 429) {
            auto retry = UnsignedHeader(response, L"Retry-After");
            ApiRemaining = 0;
            ApiReset = Clock::now() + std::chrono::seconds(retry ? std::min<std::int64_t>(*retry, 86400) : 60);
        }
    }
    if (status >= 300 && status < 400) Fail(ErrorCode::Unsupported, "Subtitle redirects are not followed.", status);
    auto content = response.Content();
    if (!content) {
        if (status < 200 || status >= 300) return {status, {}};
        Fail(ErrorCode::Protocol, "Subtitle response has no content.");
    }
    if (auto length = content.Headers().ContentLength(); length && length.Value() > maximum)
        Fail(ErrorCode::TooLarge, "Subtitle response exceeds its size limit.");
    auto stream = Await(content.ReadAsInputStreamAsync(), stop, deadline);
    std::vector<std::uint8_t> bytes;
    while (true) {
        Check(stop, deadline);
        // Read one byte past the permitted budget to distinguish EOF from an
        // oversized chunked/decompressed response without allocating unboundedly.
        auto requested = static_cast<std::uint32_t>(std::min<std::size_t>(65536, maximum - bytes.size() + 1));
        auto buffer = Await(stream.ReadAsync(Buffer(requested), requested, InputStreamOptions::Partial), stop, deadline);
        const auto count = buffer.Length();
        if (count == 0) break;
        if (count > maximum - bytes.size()) Fail(ErrorCode::TooLarge, "Subtitle response exceeds its size limit.");
        auto oldSize = bytes.size();
        bytes.resize(oldSize + count);
        DataReader::FromBuffer(buffer).ReadBytes(winrt::array_view<std::uint8_t>(bytes.data() + oldSize, bytes.data() + bytes.size()));
    }
    stream.Close();
    return {status, std::move(bytes)};
}
IJsonValue Lookup(JsonObject const& object, wchar_t const* key) {
    return object.HasKey(key) ? object.GetNamedValue(key) : nullptr;
}
std::string String(JsonObject const& object, wchar_t const* key, std::size_t limit,
    bool required = false) {
    auto value = Lookup(object, key);
    if (!value || value.ValueType() == JsonValueType::Null) {
        if (required) Fail(ErrorCode::Protocol, "Subtitle response is missing a required field.");
        return {};
    }
    if (value.ValueType() != JsonValueType::String)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid text field.");
    auto text = winrt::to_string(value.GetString());
    if (text.size() > limit || text.find('\0') != std::string::npos)
        Fail(ErrorCode::Protocol, "Subtitle response contains an oversized text field.");
    return text;
}
std::optional<double> Number(JsonObject const& object, wchar_t const* key) {
    auto value = Lookup(object, key);
    if (!value || value.ValueType() == JsonValueType::Null) return {};
    if (value.ValueType() != JsonValueType::Number || !std::isfinite(value.GetNumber()))
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid numeric field.");
    return value.GetNumber();
}
std::int64_t Integer(JsonObject const& object, wchar_t const* key, std::int64_t fallback = -1) {
    auto number = Number(object, key);
    if (!number) {
        if (fallback >= 0) return fallback;
        Fail(ErrorCode::Protocol, "Subtitle response is missing a numeric field.");
    }
    if (std::floor(*number) != *number || *number < -9007199254740991.0 || *number > 9007199254740991.0)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid integer.");
    return static_cast<std::int64_t>(*number);
}
std::optional<bool> Boolean(JsonObject const& object, wchar_t const* key) {
    auto value = Lookup(object, key);
    if (!value || value.ValueType() == JsonValueType::Null) return {};
    if (value.ValueType() != JsonValueType::Boolean) Fail(ErrorCode::Protocol, "Invalid subtitle flag.");
    return value.GetBoolean();
}
JsonObject Object(IJsonValue const& value) {
    if (!value || value.ValueType() != JsonValueType::Object)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid object.");
    return value.GetObject();
}
JsonObject ParseJson(std::vector<std::uint8_t> const& bytes) {
    JsonObject root{nullptr};
    try {
        std::string text(bytes.begin(), bytes.end());
        if (!JsonObject::TryParse(winrt::to_hstring(text), root))
            Fail(ErrorCode::Protocol, "Subtitle service returned invalid JSON.");
    } catch (winrt::hresult_error const&) {
        Fail(ErrorCode::Protocol, "Subtitle service returned invalid JSON.");
    }
    return root;
}
std::string ResetTime(JsonObject const& root) {
    auto text = String(root, L"reset_time_utc", 64);
    // Expose only an ISO-8601-shaped timestamp, never arbitrary server text.
    if (!std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || c == '-' || c == ':' || c == 'T' || c == 'Z' || c == '.' || c == '+';
    })) return {};
    return text;
}
JsonObject ParseApi(RawResponse const& response) {
    if (response.status >= 200 && response.status < 300) return ParseJson(response.bytes);
    std::optional<std::int64_t> remaining;
    std::string reset;
    bool invalidToken{};
    try {
        auto body = ParseJson(response.bytes);
        if (Number(body, L"remaining")) remaining = Integer(body, L"remaining");
        reset = ResetTime(body);
        invalidToken = String(body, L"message", 1024) == "invalid token";
    } catch (...) {
        // HTML/error-proxy bodies do not conceal the useful HTTP status.
    }
    ErrorCode code = ErrorCode::Connection;
    char const* message = "OpenSubtitles returned an HTTP error.";
    if (response.status == 401 || response.status == 403 || invalidToken) {
        code = ErrorCode::Authentication; message = "OpenSubtitles rejected the API key or user token.";
    } else if (remaining && *remaining < 0) {
        code = ErrorCode::QuotaExceeded; message = "OpenSubtitles download quota is exhausted.";
    } else if (response.status == 429) {
        code = ErrorCode::RateLimited; message = "OpenSubtitles request limit reached. Try again later.";
    } else if (response.status == 404) {
        code = ErrorCode::NotFound; message = "The selected subtitle was not found.";
    } else if (response.status == 406) {
        code = ErrorCode::Protocol; message = "OpenSubtitles rejected this request; check the account quota and token.";
    }
    throw Failure{code, message, response.status, remaining, std::move(reset)};
}
JsonArray Array(IJsonValue const& value) {
    if (!value || value.ValueType() != JsonValueType::Array)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid list.");
    return value.GetArray();
}
std::string Filename(std::string const& raw) {
    if (raw.empty() || raw.size() > 240) Fail(ErrorCode::Unsupported, "Subtitle filename is not supported.");
    // Do not strip traversal to a leaf: reject it. The caller never needs a
    // server-chosen directory, drive, ADS stream, trailing-dot alias or device.
    for (unsigned char c : raw) {
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            Fail(ErrorCode::Unsupported, "Subtitle filename is not a safe Windows leaf.");
    }
    if (raw.back() == '.' || raw.back() == ' ' || raw == "." || raw == "..")
        Fail(ErrorCode::Unsupported, "Subtitle filename is not a safe Windows leaf.");
    auto lower = LowerAscii(raw);
    auto dot = lower.rfind('.');
    if (dot == std::string::npos) Fail(ErrorCode::Unsupported, "Subtitle file has no supported extension.");
    const auto extension = lower.substr(dot);
    constexpr std::string_view allowed[]{".srt", ".ass", ".ssa", ".vtt", ".sub", ".smi", ".sami", ".lrc"};
    if (std::find(std::begin(allowed), std::end(allowed), extension) == std::end(allowed))
        Fail(ErrorCode::Unsupported, "Subtitle archives and this file type are not supported.");
    // Prefixing every leaf also avoids DOS reserved names and same-title aliases.
    return "subtitle-" + raw;
}
template<class T, class Work> Result<T> Run(Work work) {
    try {
        Apartment apartment;
        return Result<T>{work()};
    } catch (Failure const& failure) {
        return {{}, failure.code, failure.message, failure.status, failure.remaining, failure.resetTimeUtc};
    } catch (winrt::hresult_error const&) {
        // Do not surface HRESULT messages: network components may include URLs.
        return {{}, ErrorCode::Connection, "The subtitle request could not be completed.", 0};
    } catch (std::bad_alloc const&) {
        return {{}, ErrorCode::TooLarge, "Not enough memory for the subtitle response.", 0};
    } catch (...) {
        return {{}, ErrorCode::Protocol, "The subtitle response could not be processed.", 0};
    }
}
}

Result<LoginTicket> Login(std::string const& key,std::string const& username,
    std::string const& password,std::stop_token stop) {
    return Run<LoginTicket>([&] {
        ValidateCredentials(key,{});
        if (username.empty() || username.size()>256 || password.empty() || password.size()>4096 ||
            username.find('\0')!=std::string::npos || password.find('\0')!=std::string::npos)
            Fail(ErrorCode::InvalidInput,"Enter an OpenSubtitles username and password within the supported limits.");
        // Explicit attempts only; no automatic authentication retries. Separate
        // login window avoids sending rapid failed credentials across windows.
        static std::mutex loginMutex;
        static Clock::time_point nextLogin{};
        {
            std::lock_guard lock(loginMutex);
            if (Clock::now()<nextLogin) Fail(ErrorCode::RateLimited,"Wait before another OpenSubtitles login attempt.");
            nextLogin=Clock::now()+std::chrono::seconds(1);
        }
        JsonObject body;
        body.Insert(L"username",JsonValue::CreateStringValue(winrt::to_hstring(username)));
        body.Insert(L"password",JsonValue::CreateStringValue(winrt::to_hstring(password)));
        auto root=ParseApi(Request(Uri(L"https://api.opensubtitles.com/api/v1/login"),key,{},
            body.Stringify(),true,JsonLimit,stop));
        auto token=String(root,L"token",8192,true);
        if (token.empty()) Fail(ErrorCode::Protocol,"OpenSubtitles returned no session token.");
        ValidateCredentials(key,token);
        auto base=String(root,L"base_url",256);
        if (!base.empty() && LowerAscii(base)!="api.opensubtitles.com")
            Fail(ErrorCode::Unsupported,"The server accepted login but requested an unsupported alternate API host. No local session was retained.");
        return LoginTicket{std::move(token)};
    });
}
Result<bool> Logout(std::string const& key,std::string const& jwt,std::stop_token stop) {
    return Run<bool>([&] {
        ValidateCredentials(key,jwt);
        if (jwt.empty()) Fail(ErrorCode::InvalidInput,"No OpenSubtitles session is available to log out.");
        auto response=Request(Uri(L"https://api.opensubtitles.com/api/v1/logout"),key,jwt,{},false,JsonLimit,stop,true);
        // Successful DELETE can legitimately have no JSON body (HTTP 204).
        if (response.status<200 || response.status>=300) (void)ParseApi(response);
        return true;
    });
}

Result<std::vector<SearchResult>> Search(std::string const& key,
    std::string const& jwt, std::string const& query, std::string const& language,
    std::stop_token stop, std::string const& movieHash) {
    std::int64_t pages = 1;
    auto result = Run<std::vector<SearchResult>>([&] {
        ValidateCredentials(key, jwt);
        if (query.size() > 1024 || query.find('\0') != std::string::npos || (query.empty() && movieHash.empty()))
            Fail(ErrorCode::InvalidInput, "Enter a subtitle query of at most 1024 UTF-8 bytes.");
        if (!movieHash.empty() && (movieHash.size() != 16 ||
            !std::all_of(movieHash.begin(), movieHash.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }))) Fail(ErrorCode::InvalidInput, "Invalid OpenSubtitles movie hash.");
        if (language.size() > 256) Fail(ErrorCode::InvalidInput, "Too many subtitle languages.");
        std::set<std::string> languages;
        std::size_t begin{};
        auto input = language.empty() ? std::string("en") : language;
        do {
            auto end = input.find(',', begin);
            auto part = input.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            const auto first = part.find_first_not_of(" \t");
            const auto last = part.find_last_not_of(" \t");
            if (first == std::string::npos) Fail(ErrorCode::InvalidInput, "Enter comma-separated subtitle language codes.");
            part = LowerAscii(part.substr(first, last - first + 1));
            if (part.size() > 16 || !std::all_of(part.begin(), part.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || c == '-';
            })) Fail(ErrorCode::InvalidInput, "Enter comma-separated subtitle language codes.");
            languages.insert(std::move(part));
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (begin <= input.size());
        if (languages.size() > 8) Fail(ErrorCode::InvalidInput, "Choose at most eight subtitle languages.");
        std::string joined;
        for (auto const& item : languages) { if (!joined.empty()) joined += ','; joined += item; }
        // Alphabetical parameter order follows the upstream CDN-cache guidance.
        std::wstring url = L"https://api.opensubtitles.com/api/v1/subtitles?languages=";
        url += Uri::EscapeComponent(winrt::to_hstring(joined)).c_str();
        if (!movieHash.empty()) { url += L"&moviehash="; url += winrt::to_hstring(LowerAscii(movieHash)).c_str(); }
        if (!query.empty()) { url += L"&query="; url += Uri::EscapeComponent(winrt::to_hstring(query)).c_str(); }
        auto root = ParseApi(Request(Uri(url), key, jwt, {}, false, JsonLimit, stop));
        pages = Integer(root, L"total_pages", 1);
        if (pages < 0 || pages > 1000000) Fail(ErrorCode::Protocol, "Invalid subtitle page count.");
        auto values = Array(Lookup(root, L"data"));
        if (values.Size() > MaximumResults) Fail(ErrorCode::TooLarge, "Too many subtitle search results.");
        std::vector<SearchResult> choices;
        std::set<std::int64_t> seen;
        for (auto const& value : values) {
            if (stop.stop_requested()) Fail(ErrorCode::Cancelled, "Subtitle request cancelled.");
            auto subtitle = Object(value);
            if (String(subtitle, L"type", 64, true) != "subtitle") continue;
            auto attributes = Object(Lookup(subtitle, L"attributes"));
            auto files = Array(Lookup(attributes, L"files"));
            if (files.Size() > MaximumFiles) Fail(ErrorCode::TooLarge, "Too many files in a subtitle result.");
            auto lang = String(attributes, L"language", 64, true);
            auto uploaded = String(attributes, L"upload_date", 64);
            auto rating = Number(attributes, L"ratings");
            auto hearing = Boolean(attributes, L"hearing_impaired");
            auto downloads = Integer(attributes, L"download_count", 0);
            if (downloads < 0) Fail(ErrorCode::Protocol, "Invalid subtitle download count.");
            std::string type;
            if (rating) type = "Rating " + std::to_string(*rating);
            if (hearing) {
                if (!type.empty()) type += " | ";
                type += *hearing ? "Hearing impaired: yes" : "Hearing impaired: no";
            }
            for (auto const& fileValue : files) {
                auto file = Object(fileValue);
                auto id = Integer(file, L"file_id");
                if (id <= 0) Fail(ErrorCode::Protocol, "Subtitle result has an invalid file identity.");
                if (!seen.insert(id).second) continue;
                auto name = String(file, L"file_name", 4096, true);
                SearchResult choice;
                choice.id = id;
                choice.filename = name;
                choice.title = name.empty() ? "[No title]" : name;
                choice.type = type;
                choice.language = lang;
                choice.uploadTime = uploaded;
                choice.rating = rating;
                choice.hearingImpaired = hearing;
                choice.downloadCount = downloads;
                choices.push_back(std::move(choice));
                if (choices.size() > 400) Fail(ErrorCode::TooLarge, "Too many subtitle file choices.");
            }
        }
        return choices;
    });
    if (result) result.totalPages = pages;
    return result;
}
Result<DownloadTicket> RequestDownload(std::string const& key,
    std::string const& jwt, std::int64_t fileId, std::stop_token stop) {
    return Run<DownloadTicket>([&] {
        ValidateCredentials(key, jwt);
        if (fileId <= 0 || fileId > 9007199254740991LL) Fail(ErrorCode::InvalidInput, "Invalid subtitle file selection.");
        JsonObject body;
        body.Insert(L"file_id", JsonValue::CreateNumberValue(static_cast<double>(fileId)));
        auto root = ParseApi(Request(Uri(L"https://api.opensubtitles.com/api/v1/download"), key, jwt,
            body.Stringify(), true, JsonLimit, stop));
        DownloadTicket ticket;
        ticket.remaining = Integer(root, L"remaining");
        ticket.requests = Integer(root, L"requests");
        ticket.resetTimeUtc = ResetTime(root);
        if (ticket.remaining < 0)
            throw Failure{ErrorCode::QuotaExceeded, "OpenSubtitles download quota is exhausted.", 200, ticket.remaining, ticket.resetTimeUtc};
        if (ticket.requests < 0) Fail(ErrorCode::Protocol, "Invalid subtitle download quota.");
        ticket.resetTime = String(root, L"reset_time", 128);
        ticket.url = String(root, L"link", 8192, true);
        (void)DownloadUri(ticket.url);
        ticket.filename = Filename(String(root, L"file_name", 240, true));
        if (stop.stop_requested()) Fail(ErrorCode::Cancelled, "Subtitle request cancelled.");
        return ticket;
    });
}
Result<std::vector<std::uint8_t>> Download(std::string const& url, std::stop_token stop) {
    return Run<std::vector<std::uint8_t>>([&] {
        auto response = Request(DownloadUri(url), {}, {}, {}, false, SubtitleLimit, stop);
        if (response.status == 404) Fail(ErrorCode::NotFound, "Subtitle download link is no longer available.", 404);
        if (response.status == 429) Fail(ErrorCode::RateLimited, "Subtitle download rate limit reached.", 429);
        if (response.status < 200 || response.status >= 300)
            Fail(ErrorCode::Connection, "The subtitle file could not be downloaded.", response.status);
        if (response.bytes.empty()) Fail(ErrorCode::Protocol, "Downloaded subtitle is empty.");
        return std::move(response.bytes);
    });
}
}
