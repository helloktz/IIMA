// Windows adaptation of IINA AssrtSubtitle.swift / OnlineSubtitle.swift.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0.
#include "pch.h"
#include "AssrtClient.h"
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

namespace iima::assrt {
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
};
[[noreturn]] void Fail(ErrorCode code, char const* message, int status = 0) {
    throw Failure{code, message, status};
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
void ValidateToken(std::string const& token) {
    if (token.size() != 32 || !std::all_of(token.begin(), token.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    })) Fail(ErrorCode::InvalidInput, "Enter a valid 32-character Assrt token.");
}
Uri DownloadUri(std::string const& value) {
    if (value.empty() || value.size() > 8192 || value.find('\0') != std::string::npos)
        Fail(ErrorCode::Unsupported, "Subtitle download address is not supported.");
    Uri uri{winrt::to_hstring(value)};
    auto host = LowerAscii(winrt::to_string(uri.Host()));
    if (uri.SchemeName() != L"https" || uri.Port() != 443 ||
        !uri.UserName().empty() || !uri.Password().empty() || !uri.Fragment().empty() ||
        !(host == "assrt.net" || host.ends_with(".assrt.net")))
        Fail(ErrorCode::Unsupported, "Only HTTPS Assrt subtitle downloads are supported.");
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
std::vector<std::uint8_t> Request(Uri const& uri, std::string const& token,
    std::vector<std::pair<winrt::hstring, winrt::hstring>> const& form,
    bool post, std::size_t maximum, std::stop_token stop) {
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    Check(stop, deadline);
    Client client;
    HttpRequestMessage request(post ? HttpMethod::Post() : HttpMethod::Get(), uri);
    // Header is never attached to a content/download request. No caller-provided
    // endpoint is accepted for authenticated API POST operations.
    if (!token.empty()) {
        if (!post || uri.Host() != L"api.assrt.net" || uri.SchemeName() != L"https" || uri.Port() != 443)
            Fail(ErrorCode::InvalidInput, "Invalid subtitle API destination.");
        request.Headers().Authorization(winrt::Windows::Web::Http::Headers::HttpCredentialsHeaderValue(
            L"Bearer", winrt::to_hstring(token)));
    }
    if (post) {
        auto values = winrt::single_threaded_map<winrt::hstring, winrt::hstring>();
        for (auto const& [key, value] : form) values.Insert(key, value);
        request.Content(HttpFormUrlEncodedContent(values));
    }
    auto response = Await(client.http.SendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead), stop, deadline);
    const auto status = static_cast<int>(response.StatusCode());
    if (status == 401 || status == 403) Fail(ErrorCode::Authentication, "Assrt rejected authentication.");
    if (status == 429) Fail(ErrorCode::RateLimited, "Assrt request limit reached. Try again later.");
    if (status == 404) Fail(ErrorCode::NotFound, "Subtitle resource was not found.");
    if (status >= 300 && status < 400) Fail(ErrorCode::Unsupported, "Subtitle redirects are not followed.");
    if (status < 200 || status >= 300) Fail(ErrorCode::Connection, "Subtitle service returned an HTTP error.");
    auto content = response.Content();
    if (!content) Fail(ErrorCode::Protocol, "Subtitle response has no content.");
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
    return bytes;
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
std::int64_t Integer(JsonObject const& object, wchar_t const* key) {
    auto value = Lookup(object, key);
    if (!value || value.ValueType() != JsonValueType::Number)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid numeric field.");
    auto number = value.GetNumber();
    // JSON number uses binary64. IDs larger than 2^53-1 cannot be represented
    // exactly; do not silently round and request a different subtitle.
    if (!std::isfinite(number) || std::floor(number) != number || number < 0 || number > 9007199254740991.0)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid numeric field.");
    return static_cast<std::int64_t>(number);
}
JsonObject Object(IJsonValue const& value) {
    if (!value || value.ValueType() != JsonValueType::Object)
        Fail(ErrorCode::Protocol, "Subtitle response contains an invalid object.");
    return value.GetObject();
}
JsonObject Parse(std::vector<std::uint8_t> const& bytes) {
    JsonObject root{nullptr};
    try {
        std::string text(bytes.begin(), bytes.end());
        if (!JsonObject::TryParse(winrt::to_hstring(text), root))
            Fail(ErrorCode::Protocol, "Subtitle service returned invalid JSON.");
    } catch (winrt::hresult_error const&) {
        Fail(ErrorCode::Protocol, "Subtitle service returned invalid JSON.");
    }
    const auto status = Integer(root, L"status");
    if (status != 0) {
        switch (status) {
        case 1: case 20001: Fail(ErrorCode::Authentication, "Assrt rejected the token or account.", static_cast<int>(status));
        case 101: Fail(ErrorCode::InvalidInput, "Subtitle search query is too short.", 101);
        case 20900: Fail(ErrorCode::NotFound, "The selected subtitle is no longer available.", 20900);
        case 30900: Fail(ErrorCode::RateLimited, "Assrt request limit reached. Try again later.", 30900);
        case 30000: case 30001: case 30002: case 30300:
            Fail(ErrorCode::Connection, "Assrt is temporarily unavailable.", static_cast<int>(status));
        default:
            Fail(ErrorCode::Protocol, "Assrt returned an unsupported status.", status <= std::numeric_limits<int>::max() ? static_cast<int>(status) : 0);
        }
    }
    return Object(Lookup(root, L"sub"));
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
        return {{}, failure.code, failure.message, failure.status};
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

Result<std::vector<SearchResult>> Search(std::string const& token,
    std::string const& query, std::stop_token stop) {
    return Run<std::vector<SearchResult>>([&] {
        ValidateToken(token);
        if (query.empty() || query.size() > 1024 || query.find('\0') != std::string::npos)
            Fail(ErrorCode::InvalidInput, "Enter a subtitle search query of at most 1024 UTF-8 bytes.");
        auto sub = Parse(Request(Uri(L"https://api.assrt.net/v1/sub/search"), token,
            {{L"q", winrt::to_hstring(query)}, {L"is_file", L"1"}, {L"no_muxer", L"1"}}, true, JsonLimit, stop));
        std::vector<SearchResult> results;
        auto values = Lookup(sub, L"subs");
        // Upstream Assrt returns {} for empty results (and some deployments
        // return an empty sub object); neither is a network failure.
        if (!values || (values.ValueType() == JsonValueType::Object && values.GetObject().Size() == 0)) return results;
        auto array = Array(values);
        if (array.Size() > MaximumResults) Fail(ErrorCode::TooLarge, "Too many subtitle search results.");
        for (auto const& value : array) {
            if (stop.stop_requested()) Fail(ErrorCode::Cancelled, "Subtitle request cancelled.");
            auto item = Object(value);
            SearchResult result;
            result.id = Integer(item, L"id");
            if (result.id == 0) Fail(ErrorCode::Protocol, "Subtitle result has an invalid identity.");
            result.title = String(item, L"native_name", 4096, true);
            if (result.title.empty()) result.title = "[No title]";
            result.uploadTime = String(item, L"upload_time", 128, true);
            result.type = String(item, L"subtype", 256);
            if (result.type.empty()) result.type = "Unknown";
            if (auto language = Lookup(item, L"lang"); language && language.ValueType() != JsonValueType::Null)
                result.language = String(Object(language), L"desc", 256);
            results.push_back(std::move(result));
        }
        return results;
    });
}
Result<std::vector<DownloadFile>> Details(std::string const& token,
    std::int64_t id, std::stop_token stop) {
    return Run<std::vector<DownloadFile>>([&] {
        ValidateToken(token);
        if (id <= 0 || id > 9007199254740991LL) Fail(ErrorCode::InvalidInput, "Invalid subtitle selection.");
        auto sub = Parse(Request(Uri(L"https://api.assrt.net/v1/sub/detail"), token,
            {{L"id", winrt::to_hstring(std::to_string(id))}}, true, JsonLimit, stop));
        auto values = Array(Lookup(sub, L"subs"));
        if (values.Size() != 1) Fail(ErrorCode::Protocol, "Subtitle details did not identify exactly one result.");
        auto item = Object(values.GetAt(0));
        std::vector<DownloadFile> files;
        auto append = [&](std::string const& name, std::string const& url) {
            (void)DownloadUri(url);
            files.push_back({Filename(name), url});
        };
        if (auto list = Lookup(item, L"filelist"); list && list.ValueType() != JsonValueType::Null) {
            auto array = Array(list);
            if (array.Size() > MaximumFiles) Fail(ErrorCode::TooLarge, "Too many files in the selected subtitle.");
            for (auto const& value : array) {
                auto file = Object(value);
                append(String(file, L"f", 240, true), String(file, L"url", 8192, true));
            }
        } else {
            append(String(item, L"filename", 240, true), String(item, L"url", 8192, true));
        }
        if (files.empty()) Fail(ErrorCode::NotFound, "The selected subtitle has no downloadable files.");
        if (stop.stop_requested()) Fail(ErrorCode::Cancelled, "Subtitle request cancelled.");
        return files;
    });
}
Result<std::vector<std::uint8_t>> Download(std::string const& url, std::stop_token stop) {
    return Run<std::vector<std::uint8_t>>([&] {
        auto bytes = Request(DownloadUri(url), {}, {}, false, SubtitleLimit, stop);
        if (bytes.empty()) Fail(ErrorCode::Protocol, "Downloaded subtitle is empty.");
        return bytes;
    });
}
}
