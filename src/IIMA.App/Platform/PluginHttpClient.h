// IINA JavascriptAPIHttp request/hostIsValid and JavascriptPluginInstance.canAccess.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../../IIMA.Core/Plugins/PluginManifest.h"
#include <stop_token>
#include <memory>
#include <string>

namespace iima {
class PluginFileStore;
class PluginHttpClient final {
public:
    // No network activity. The owner must separately obtain execution/network
    // consent for this immutable manifest snapshot; its declaration is not consent.
    explicit PluginHttpClient(PluginManifest manifest, std::shared_ptr<PluginFileStore> files = {});
    // Background worker only. {method,url,options:{params?,headers?,data?}}, maps
    // of string->string; data is form encoded. Optional destination requests a
    // private-file download (no response body is exposed on success). Returns {ok,response:{statusCode,
    // reason,text,data}}. Non-2xx is a response, transport/validation fails throw.
    std::string Request(std::string requestJson, std::stop_token stop);
private:
    PluginManifest manifest_;
    std::shared_ptr<PluginFileStore> files_;
};
// Restricted HTTPS adapter, NOT an SSRF or hostile-code sandbox. OS DNS can map
// an allowed DNS name to private addresses, and WinRT does not expose an address-
// pinned transport here. No IP literals/local hostnames, redirects, shared cookie
// jar, proxy, credential lookup, XML-RPC or binary response API. Downloads
// atomically write at most 1 MiB to the supplied private file store, never execute.
}
