// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UpdateChecker {

#if !defined(AZAHAR_UPDATE_REPO_OWNER)
#define AZAHAR_UPDATE_REPO_OWNER "diegolix29"
#endif
#if !defined(AZAHAR_UPDATE_REPO_NAME)
#define AZAHAR_UPDATE_REPO_NAME "AzaharTrigger"
#endif

constexpr char RepoOwner[] = AZAHAR_UPDATE_REPO_OWNER;
constexpr char RepoName[] = AZAHAR_UPDATE_REPO_NAME;

std::string ExtractBuildVersionFromAsset(const std::string& asset_name);

struct ReleaseAsset {
    std::string name;
    std::string browser_download_url;
    std::uint64_t size = 0;
};

struct ReleaseInfo {
    std::string tag_name;
    std::string html_url;
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;
};

std::optional<std::string> GetLatestRelease(bool include_prereleases);

std::optional<ReleaseInfo> GetLatestReleaseInfo(bool include_prereleases);

std::optional<std::string> GetLatestBuildVersion(bool include_prereleases);

} // namespace UpdateChecker
