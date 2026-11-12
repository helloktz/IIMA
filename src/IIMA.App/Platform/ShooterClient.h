// IINA ShooterSubtitle.swift Windows adaptation.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "AssrtClient.h"
namespace iima::shooter {
struct SearchResult {
    assrt::SearchResult row;
    assrt::DownloadFile file;
    int delayMilliseconds{}; // upstream exposes Delay but does not apply it
};
// Explicitly submitted movie fingerprint + user-edited title, never full path.
// Worker-only. Fixed Shooter API; no keys/cookies, HTTPS, no redirects.
assrt::Result<std::vector<SearchResult>> Search(std::string const& movieHash,
    std::string const& title, std::stop_token);
assrt::Result<std::vector<std::uint8_t>> Download(std::string const& url, std::stop_token);
}
