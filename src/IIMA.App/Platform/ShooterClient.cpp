// IINA Shooter.Fetcher.request and Subtitle.download; bounded Windows transport.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "ShooterClient.h"
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#pragma comment(lib,"winhttp.lib")
namespace iima::shooter {
namespace {
using namespace winrt::Windows::Data::Json;
using Clock = std::chrono::steady_clock;
struct Failure { assrt::ErrorCode code; char const* message; };
struct HandleCloser { void operator()(void* h) const noexcept { if (h) WinHttpCloseHandle(h); } };
using Handle = std::unique_ptr<void,HandleCloser>;
void Require(bool condition, char const* message) {
    if (!condition) throw Failure{assrt::ErrorCode::Connection,message};
}
std::string Lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    return value;
}
winrt::Windows::Foundation::Uri Address(std::string const& url) {
    if (url.empty() || url.size() > 8192 || url.find('\0') != std::string::npos)
        throw Failure{assrt::ErrorCode::InvalidInput,"Invalid Shooter address."};
    winrt::Windows::Foundation::Uri uri(winrt::to_hstring(url));
    auto host = Lower(winrt::to_string(uri.Host()));
    if (uri.SchemeName() != L"https" || uri.Port() != 443 || !uri.UserName().empty() ||
        !uri.Password().empty() || !uri.Fragment().empty() ||
        !(host == "shooter.cn" || host.ends_with(".shooter.cn")))
        throw Failure{assrt::ErrorCode::Unsupported,"Only HTTPS shooter.cn subtitle addresses are supported."};
    return uri;
}
std::vector<std::uint8_t> Request(std::string const& url, std::string const& body,
    std::size_t limit, std::stop_token stop) {
    auto check = [stop,deadline=Clock::now()+std::chrono::seconds(30)] {
        if (stop.stop_requested()) throw Failure{assrt::ErrorCode::Cancelled,"Shooter request cancelled."};
        if (Clock::now() >= deadline) throw Failure{assrt::ErrorCode::TimedOut,"Shooter request timed out."};
    };
    check(); auto uri = Address(url);
    Handle session(WinHttpOpen(L"IIMA/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    Require(bool(session),"Could not create Shooter transport.");
    Require(WinHttpSetTimeouts(session.get(),5000,5000,5000,5000),"Could not configure transport timeouts.");
    Handle connection(WinHttpConnect(session.get(),uri.Host().c_str(),443,0));
    Require(bool(connection),"Could not connect to Shooter.");
    auto target = std::wstring(uri.Path())+std::wstring(uri.Query());
    Handle request(WinHttpOpenRequest(connection.get(),body.empty() ? L"GET" : L"POST",target.c_str(),
        nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    Require(bool(request),"Could not create Shooter request.");
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
    Require(WinHttpSetOption(request.get(),WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)),"Could not restrict Shooter transport.");
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    Require(WinHttpSetOption(request.get(),WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon)),"Could not disable automatic credentials.");
    check();
    auto headers = body.empty() ? L"" : L"Content-Type: application/x-www-form-urlencoded\r\n";
    Require(WinHttpSendRequest(request.get(),headers,static_cast<DWORD>(-1),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0),"Shooter request failed.");
    check(); Require(WinHttpReceiveResponse(request.get(),nullptr),"Shooter response failed.");
    DWORD status{}, size=sizeof(status);
    Require(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"Shooter status unavailable.");
    if (status < 200 || status >= 300)
        throw Failure{assrt::ErrorCode::Protocol,"Shooter rejected the request or redirected it. No automatic retry."};
    std::vector<std::uint8_t> bytes;
    while (true) {
        check(); std::uint8_t block[16384]; DWORD read{};
        Require(WinHttpReadData(request.get(),block,sizeof(block),&read),"Shooter body read failed.");
        check(); if (!read) break;
        if (read > limit-bytes.size()) throw Failure{assrt::ErrorCode::TooLarge,"Shooter response exceeds its limit."};
        bytes.insert(bytes.end(),block,block+read);
    }
    if (bytes.empty()) throw Failure{assrt::ErrorCode::Protocol,"Shooter returned an empty response."};
    return bytes;
}
std::string Text(JsonObject const& object, wchar_t const* name, std::size_t maximum, bool required) {
    if (!object.HasKey(name)) {
        if (required) throw Failure{assrt::ErrorCode::Protocol,"Shooter result is missing a field."};
        return {};
    }
    auto value=object.GetNamedValue(name);
    if (value.ValueType()!=JsonValueType::String)
        throw Failure{assrt::ErrorCode::Protocol,"Shooter returned an invalid text field."};
    auto text=winrt::to_string(value.GetString());
    if (text.size()>maximum || text.find('\0')!=std::string::npos)
        throw Failure{assrt::ErrorCode::Protocol,"Shooter text exceeds its limit."};
    return text;
}
template<class T,class Work> assrt::Result<T> Run(Work work) {
    assrt::Result<T> result;
    try {
        winrt::check_hresult(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        result.value=work();
    } catch (Failure const& f) { result.error=f.code; result.message=f.message; }
      catch (...) { result.error=assrt::ErrorCode::Protocol; result.message="Could not process Shooter response."; }
    return result;
}
}
assrt::Result<std::vector<SearchResult>> Search(std::string const& hash,std::string const& title,std::stop_token stop) {
    return Run<std::vector<SearchResult>>([&] {
        if (hash.size()!=131 || title.empty() || title.size()>1024 || title.find_first_of("/\\")!=std::string::npos || title.find('\0')!=std::string::npos)
            throw Failure{assrt::ErrorCode::InvalidInput,"Shooter requires a movie hash and a title without a path."};
        for (std::size_t i=0;i<hash.size();++i) {
            bool separator=(i==32 || i==65 || i==98);
            if (separator ? hash[i]!=';' : !((hash[i]>='0' && hash[i]<='9') || (hash[i]>='a' && hash[i]<='f')))
                throw Failure{assrt::ErrorCode::InvalidInput,"Invalid Shooter movie fingerprint."};
        }
        using winrt::Windows::Foundation::Uri;
        auto form="filehash="+winrt::to_string(Uri::EscapeComponent(winrt::to_hstring(hash)))+
            "&pathinfo="+winrt::to_string(Uri::EscapeComponent(winrt::to_hstring(title)))+"&format=json";
        auto data=Request("https://www.shooter.cn/api/subapi.php",form,2*1024*1024,stop);
        std::string json(data.begin(),data.end());
        // Some service versions return a negative scalar for no match; do not
        // reinterpret it as a valid empty array without a verified contract.
        auto groups=JsonArray::Parse(winrt::to_hstring(json));
        if (groups.Size()>200) throw Failure{assrt::ErrorCode::TooLarge,"Too many Shooter results."};
        std::vector<SearchResult> result;
        for (std::uint32_t i=0;i<groups.Size();++i) {
            auto group=groups.GetObjectAt(i);
            auto desc=Text(group,L"Desc",2048,false);
            int delay{};
            if (group.HasKey(L"Delay")) {
                double value=group.GetNamedNumber(L"Delay");
                if (!std::isfinite(value) || std::floor(value)!=value || std::abs(value)>86400000)
                    throw Failure{assrt::ErrorCode::Protocol,"Invalid Shooter delay."};
                delay=static_cast<int>(value);
            }
            auto files=group.GetNamedArray(L"Files");
            if (files.Size()>8) throw Failure{assrt::ErrorCode::TooLarge,"Too many files in Shooter result."};
            for (std::uint32_t j=0;j<files.Size();++j) {
                auto file=files.GetObjectAt(j); auto ext=Lower(Text(file,L"Ext",16,true));
                if (ext!="srt" && ext!="ass" && ext!="ssa" && ext!="vtt") continue;
                auto link=Text(file,L"Link",8192,true);
                try { (void)Address(link); } catch (...) { continue; }
                if (result.size()>=400) throw Failure{assrt::ErrorCode::TooLarge,"Too many expanded Shooter files."};
                auto id=static_cast<std::int64_t>(result.size()+1);
                auto name="shooter-"+std::to_string(i+1)+"-"+std::to_string(j+1)+"."+ext;
                result.push_back({{id,desc.empty()?name:desc,ext,"","Delay: "+std::to_string(delay)+" ms (not applied)"},
                    {name,std::move(link)},delay});
            }
        }
        return result;
    });
}
assrt::Result<std::vector<std::uint8_t>> Download(std::string const& url,std::stop_token stop) {
    return Run<std::vector<std::uint8_t>>([&] { return Request(url,{},8*1024*1024,stop); });
}
}
