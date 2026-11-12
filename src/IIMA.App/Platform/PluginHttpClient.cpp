// IINA JavascriptAPIHttp.swift / JavascriptPluginInstance.canAccess adaptation.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginHttpClient.h"
#include "PluginFileStore.h"
#include "PluginXmlRpc.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Data::Json;
using namespace Windows::Storage::Streams;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Filters;
using Clock = std::chrono::steady_clock;
using Map = std::map<std::string, std::string>;
constexpr std::size_t RequestLimit = 128 * 1024;
constexpr std::size_t ResponseLimit = 1024 * 1024;
struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (FAILED(status) && status != RPC_E_CHANGED_MODE) check_hresult(status); }
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
void Check(std::stop_token stop, Clock::time_point deadline) {
    if (stop.stop_requested()) throw std::runtime_error("Plugin HTTP request cancelled.");
    if (Clock::now() >= deadline) throw std::runtime_error("Plugin HTTP request timed out.");
}
template<class Async> auto Await(Async const& task, std::stop_token stop, Clock::time_point deadline) {
    try {
        while (task.Status() == AsyncStatus::Started) {
            Check(stop, deadline); std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        Check(stop, deadline); return task.GetResults();
    } catch (...) { try { task.Cancel(); } catch (...) {} throw; }
}
std::wstring Wide(std::string_view bytes) {
    if (bytes.empty()) return {};
    auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("Plugin HTTP text must be UTF-8.");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    check_bool(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), result.data(), size));
    return result;
}
std::string Lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
bool Controls(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool JsonBudget(std::string_view text, unsigned maxDepth, unsigned maxTokens) {
    unsigned depth{}, tokens{}; bool quoted{}, escaped{};
    for (char c : text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') { quoted = true; if (++tokens > maxTokens) return false; }
        else if (c == '{' || c == '[') { if (++depth > maxDepth || ++tokens > maxTokens) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
        else if (c == ',' && ++tokens > maxTokens) return false;
    }
    return !quoted && !depth;
}
Map StringMap(JsonObject const& options, wchar_t const* name, std::size_t& total) {
    if (!options.HasKey(name)) return {};
    auto object = options.GetNamedObject(name);
    if (object.Size() > 64) throw std::invalid_argument("Plugin HTTP map exceeds 64 entries.");
    Map result;
    for (auto const& entry : object) {
        if (entry.Value().ValueType() != JsonValueType::String) throw std::invalid_argument("Plugin HTTP map values must be strings.");
        auto key = to_string(entry.Key()), value = to_string(entry.Value().GetString());
        if (key.empty() || key.size() > 256 || value.size() > 16384 || key.find('\0') != key.npos || value.find('\0') != value.npos)
            throw std::invalid_argument("Plugin HTTP map entry is invalid or too long.");
        total += key.size() + value.size();
        if (total > 65536) throw std::invalid_argument("Plugin HTTP options exceed 64 KiB.");
        result.emplace(std::move(key), std::move(value));
    }
    return result;
}
void Header(std::string const& name, std::string const& value) {
    constexpr std::string_view separators = "()<>@,;:\\\"/[]?={} \t";
    if (name.size() > 128 || value.size() > 8192 || Controls(name) || Controls(value) ||
        std::any_of(name.begin(), name.end(), [&](unsigned char c) { return c >= 127 || separators.find(static_cast<char>(c)) != separators.npos; }))
        throw std::invalid_argument("Invalid plugin HTTP header.");
    auto key = Lower(name);
    // Content framing is controlled by HttpFormUrlEncodedContent. Never import
    // browser/session cookies or permit proxy/connection routing from a plugin.
    if (key == "host" || key == "connection" || key == "cookie" || key == "cookie2" || key == "set-cookie" ||
        key == "content-length" || key == "content-type" || key == "transfer-encoding" || key == "te" || key == "trailer" ||
        key == "upgrade" || key == "expect" || key == "keep-alive" || key == "origin" || key == "referer" ||
        key == "accept-encoding" || key.starts_with("proxy-") || key.starts_with("sec-"))
        throw std::invalid_argument("This plugin HTTP header is controlled by the transport.");
}
Uri CheckedUri(std::string const& raw, PluginManifest const& manifest) {
    if (raw.empty() || raw.size() > 8192 || Controls(raw) || raw.find('\\') != raw.npos || raw.find('#') != raw.npos)
        throw std::invalid_argument("Unsupported plugin HTTP URL.");
    Uri uri(Wide(raw));
    auto host = Lower(to_string(uri.Host()));
    auto normalized = NormalizePluginDomain(host);
    auto suffix = host.substr(host.rfind('.') == host.npos ? 0 : host.rfind('.') + 1);
    // A DNS name with alphabetic top-level label excludes decimal/hex/octal IPv4
    // spellings; URI canonicalization and ASCII validation also exclude IPv6.
    if (!normalized || *normalized != host || host.find('.') == host.npos || host.find('*') != host.npos ||
        suffix.empty() || suffix.front() < 'a' || suffix.front() > 'z' ||
        host == "localhost" || host.ends_with(".localhost") || host.ends_with(".local") ||
        host.ends_with(".internal") || host.ends_with(".lan") || host.ends_with(".home") ||
        uri.SchemeName() != L"https" || uri.Port() != 443 || !uri.UserName().empty() || !uri.Password().empty() ||
        !uri.Fragment().empty() || !manifest.DeclaresNetworkHost(host))
        throw std::invalid_argument("Plugin URL is outside the permitted HTTPS domain scope.");
    auto authorityStart = raw.find("://");
    if (authorityStart == raw.npos) throw std::invalid_argument("An absolute HTTPS URL is required.");
    auto authority = std::string_view(raw).substr(authorityStart + 3);
    authority = authority.substr(0, authority.find_first_of("/?"));
    if (authority.find('@') != authority.npos) throw std::invalid_argument("URL credentials are not supported.");
    return uri;
}
struct Client {
    HttpBaseProtocolFilter filter;
    HttpClient http{nullptr};
    Client() {
        filter.AllowAutoRedirect(false); filter.AllowUI(false); filter.UseProxy(false);
        filter.CookieUsageBehavior(HttpCookieUsageBehavior::NoCookies);
        filter.ServerCredential(nullptr); filter.ProxyCredential(nullptr);
        filter.CacheControl().ReadBehavior(HttpCacheReadBehavior::NoCache);
        filter.CacheControl().WriteBehavior(HttpCacheWriteBehavior::NoCache);
        http = HttpClient(filter);
    }
    ~Client() { try { http.Close(); } catch (...) {} try { filter.Close(); } catch (...) {} }
};
std::string Perform(PluginManifest const& manifest, std::string const& json, std::stop_token stop, std::shared_ptr<PluginFileStore> const& files) {
    auto deadline = Clock::now() + std::chrono::seconds(30); Check(stop, deadline);
    if (!manifest.HasPermission(PluginPermission::NetworkRequest)) throw std::invalid_argument("Plugin network-request permission is absent.");
    if (json.empty() || json.size() > RequestLimit || !JsonBudget(json, 20, 16384))
        throw std::invalid_argument("Plugin HTTP request exceeds its structural budget.");
    auto input = JsonObject::Parse(Wide(json));
    bool rpc = input.HasKey(L"rpc");
    std::string destination;
    bool downloading = input.HasKey(L"destination");
    if (downloading) {
        if (rpc) throw std::invalid_argument("XML-RPC cannot request a file destination.");
        if (!files) throw std::invalid_argument("Private download storage is unavailable.");
        destination = to_string(input.GetNamedString(L"destination"));
        PluginFileStore::ValidateDownloadPath(destination); // before any network operation
    }
    auto method = to_string(input.GetNamedString(L"method"));
    if (method != "GET" && method != "POST" && method != "PUT" && method != "PATCH" && method != "DELETE")
        throw std::invalid_argument("Unsupported plugin HTTP method.");
    std::string rpcBody;
    if (rpc) {
        if (method != "POST" || input.HasKey(L"options")) throw std::invalid_argument("Unsupported XML-RPC request options.");
        auto call = input.GetNamedObject(L"rpc");
        if (call.Size() != 2 || !call.HasKey(L"method") || !call.HasKey(L"args"))
            throw std::invalid_argument("Invalid XML-RPC call.");
        rpcBody = PluginXmlRpc::Encode(to_string(call.GetNamedString(L"method")), to_string(call.GetNamedArray(L"args").Stringify()));
    }
    auto uri = CheckedUri(to_string(input.GetNamedString(L"url")), manifest);
    auto options = input.HasKey(L"options") ? input.GetNamedObject(L"options") : JsonObject{};
    for (auto const& option : options)
        if (option.Key() != L"params" && option.Key() != L"headers" && option.Key() != L"data")
            throw std::invalid_argument("Unsupported plugin HTTP option.");
    std::size_t optionBytes{};
    auto params = StringMap(options, L"params", optionBytes);
    auto headers = StringMap(options, L"headers", optionBytes);
    auto data = StringMap(options, L"data", optionBytes);
    if (method == "GET" && !data.empty()) throw std::invalid_argument("GET request cannot contain form data.");
    std::string query = to_string(uri.AbsoluteUri());
    for (auto const& [key, value] : params) {
        query += query.find('?') == query.npos ? '?' : '&';
        query += to_string(Uri::EscapeComponent(Wide(key))) + "=" + to_string(Uri::EscapeComponent(Wide(value)));
    }
    uri = CheckedUri(query, manifest); // recheck final size and authority after parameter encoding
    Client client;
    HttpRequestMessage request(HttpMethod(to_hstring(method)), uri);
    for (auto const& [name, value] : headers) {
        Header(name, value);
        if (!request.Headers().TryAppendWithoutValidation(to_hstring(name), Wide(value)))
            throw std::invalid_argument("Plugin HTTP header cannot be added.");
    }
    if (rpc) {
        request.Content(HttpStringContent(Wide(rpcBody), UnicodeEncoding::Utf8, L"text/xml"));
    } else if (method != "GET" && options.HasKey(L"data")) {
        auto fields = single_threaded_map<hstring, hstring>();
        for (auto const& [key, value] : data) fields.Insert(Wide(key), Wide(value));
        request.Content(HttpFormUrlEncodedContent(fields));
    }
    auto response = Await(client.http.SendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead), stop, deadline);
    auto status = static_cast<int>(response.StatusCode());
    std::string bytes;
    if (auto content = response.Content()) {
        if (auto length = content.Headers().ContentLength(); length && length.Value() > ResponseLimit)
            throw std::runtime_error("Plugin HTTP response exceeds 1 MiB.");
        auto stream = Await(content.ReadAsInputStreamAsync(), stop, deadline);
        struct CloseStream { IInputStream stream; ~CloseStream() { try { stream.Close(); } catch (...) {} } } close{stream};
        for (;;) {
            Check(stop, deadline);
            auto wanted = static_cast<std::uint32_t>(std::min<std::size_t>(65536, ResponseLimit - bytes.size() + 1));
            auto buffer = Await(stream.ReadAsync(Buffer(wanted), wanted, InputStreamOptions::Partial), stop, deadline);
            auto size = buffer.Length(); if (!size) break;
            if (size > ResponseLimit - bytes.size()) throw std::runtime_error("Plugin HTTP response exceeds 1 MiB.");
            std::vector<std::uint8_t> chunk(size); DataReader::FromBuffer(buffer).ReadBytes(chunk);
            bytes.append(reinterpret_cast<char const*>(chunk.data()), chunk.size());
        }
    }
    Check(stop, deadline);
    // HTTP failures never replace a destination. Successful downloads are binary:
    // no UTF-8/JSON decoding of the bytes and no executable/automatic-open path.
    bool downloaded = downloading && status >= 200 && status < 300;
    if (downloaded) {
        std::vector<std::uint8_t> content(bytes.begin(), bytes.end());
        files->WriteDownload(destination, content, stop);
        bytes.clear();
    }
    hstring text;
    bool decodable = true;
    try { text = Wide(bytes); }
    catch (std::invalid_argument const&) {
        if (!downloading && !rpc) throw;
        decodable = false; // failed binary download still rejects with HTTP status
    }
    JsonValue parsed{nullptr};
    IJsonValue body = JsonValue::CreateNullValue();
    // Large/deep JSON is still returned as text but not expanded into a second
    // unbounded object tree. Invalid JSON likewise maps to data:null as upstream.
    if (!rpc && decodable && JsonBudget(bytes, 32, 16384) && JsonValue::TryParse(text, parsed)) body = parsed;
    JsonObject result;
    result.Insert(L"statusCode", JsonValue::CreateNumberValue(status));
    auto reason = response.ReasonPhrase();
    if (reason.size() > 512) reason = L"";
    result.Insert(L"reason", JsonValue::CreateStringValue(reason));
    result.Insert(L"text", decodable ? JsonValue::CreateStringValue(text) : JsonValue::CreateNullValue()); result.Insert(L"data", body);
    if (rpc && status >= 200 && status < 300) {
        try {
            auto decoded = JsonObject::Parse(Wide(PluginXmlRpc::Decode(bytes)));
            result.Insert(L"rpcFault", decoded.GetNamedValue(L"fault"));
            result.Insert(L"rpcValue", decoded.GetNamedValue(L"value"));
        } catch (...) {
            // Parsing failures do not echo hostile XML or native diagnostics.
            result.Insert(L"rpcError", JsonValue::CreateStringValue(L"Bad response"));
        }
        result.Insert(L"text", JsonValue::CreateNullValue());
        result.Insert(L"data", JsonValue::CreateNullValue());
    }
    JsonObject envelope;
    envelope.Insert(L"ok", JsonValue::CreateBooleanValue(status >= 200 && status < 300));
    envelope.Insert(L"response", result);
    auto serialized = to_string(envelope.Stringify());
    if (serialized.size() > 16 * ResponseLimit) throw std::runtime_error("Plugin HTTP result exceeds its serialization budget.");
    // Once the atomic write committed, do not turn its success into a timeout.
    // Stop may still discard the completion; cancellation is not a rollback.
    if (!downloaded) Check(stop, deadline);
    return serialized;
}
}
PluginHttpClient::PluginHttpClient(PluginManifest manifest, std::shared_ptr<PluginFileStore> files)
    : manifest_(std::move(manifest)), files_(std::move(files)) {}
std::string PluginHttpClient::Request(std::string requestJson, std::stop_token stop) {
    try { Apartment apartment; return Perform(manifest_, requestJson, stop, files_); }
    catch (hresult_error const&) {
        // Windows transport messages may contain URLs or credentials. Do not
        // reflect them into plugin logs, UI, or rejected promise diagnostics.
        if (stop.stop_requested()) throw std::runtime_error("Plugin HTTP request cancelled.");
        throw std::runtime_error("Plugin HTTP validation or transport failed.");
    }
}
}
