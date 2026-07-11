// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UpdateChecker {

// The GitHub repository ("owner/name") that release information and update
// assets are pulled from. This defaults to the upstream Azahar repository,
// but can be overridden at compile time (e.g. by forks) with
// -DAZAHAR_UPDATE_REPO_OWNER=... -DAZAHAR_UPDATE_REPO_NAME=...
#if !defined(AZAHAR_UPDATE_REPO_OWNER)
#define AZAHAR_UPDATE_REPO_OWNER "diegolix29"
#endif
#if !defined(AZAHAR_UPDATE_REPO_NAME)
#define AZAHAR_UPDATE_REPO_NAME "AzaharTrigger"
#endif

constexpr char RepoOwner[] = AZAHAR_UPDATE_REPO_OWNER;
constexpr char RepoName[] = AZAHAR_UPDATE_REPO_NAME;

/// A single downloadable file attached to a GitHub release.
struct ReleaseAsset {
    std::string name;                 ///< e.g. "azahar-windows-msvc-v2100.zip"
    std::string browser_download_url; ///< direct download URL
    std::uint64_t size = 0;           ///< size in bytes, as reported by GitHub
};

/// A GitHub release, with the assets attached to it.
struct ReleaseInfo {
    std::string tag_name;
    std::string html_url;
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;
};

/// Returns just the tag name of the latest applicable release, preserving the
/// pre-existing behavior used by the "check for updates" prompt.
std::optional<std::string> GetLatestRelease(bool include_prereleases);

/// Returns full information (tag, assets, etc.) about the latest applicable
/// release. This is what the self-updater uses to find a download for the
/// current platform.
std::optional<ReleaseInfo> GetLatestReleaseInfo(bool include_prereleases);

} // namespace UpdateChecker
