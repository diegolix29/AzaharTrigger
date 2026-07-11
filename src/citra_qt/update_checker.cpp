// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <optional>
#include <string>
#include <fmt/format.h>
#include <httplib.h>
#include <json.hpp>
#include "common/logging/log.h"
#include "update_checker.h"

namespace {

std::optional<std::string> GetResponse(const std::string& url, const std::string& path) {
    constexpr std::size_t timeout_seconds = 15;

    std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(url);
    client->set_connection_timeout(timeout_seconds);
    client->set_read_timeout(timeout_seconds);
    client->set_write_timeout(timeout_seconds);

    if (client == nullptr) {
        LOG_ERROR(Frontend, "Invalid URL {}{}", url, path);
        return {};
    }

    // GitHub requires a User-Agent header on API requests, or it will respond
    // with 403 Forbidden.
    httplib::Headers headers{
        {"User-Agent", fmt::format("{}-updater", UpdateChecker::RepoName)},
        {"Accept", "application/vnd.github+json"},
    };

    httplib::Request request{
        .method = "GET",
        .path = path,
        .headers = headers,
    };

    client->set_follow_location(true);
    const auto result = client->send(request);
    if (!result) {
        LOG_ERROR(Frontend, "GET to {}{} returned null (error: {})", url, path,
                  httplib::to_string(result.error()));
        return {};
    }

    const auto& response = result.value();
    if (response.status >= 400) {
        LOG_ERROR(Frontend, "GET to {}{} returned error status code: {}", url, path,
                  response.status);
        return {};
    }
    if (!response.headers.contains("content-type")) {
        LOG_ERROR(Frontend, "GET to {}{} returned no content", url, path);
        return {};
    }

    return response.body;
}

UpdateChecker::ReleaseInfo ParseRelease(const nlohmann::json& release_json) {
    UpdateChecker::ReleaseInfo info;
    info.tag_name = release_json.value("tag_name", std::string{});
    info.html_url = release_json.value("html_url", std::string{});
    info.prerelease = release_json.value("prerelease", false);

    if (release_json.contains("assets") && release_json.at("assets").is_array()) {
        for (const auto& asset_json : release_json.at("assets")) {
            UpdateChecker::ReleaseAsset asset;
            asset.name = asset_json.value("name", std::string{});
            asset.browser_download_url = asset_json.value("browser_download_url", std::string{});
            asset.size = asset_json.value("size", std::uint64_t{0});
            if (!asset.name.empty() && !asset.browser_download_url.empty()) {
                info.assets.push_back(std::move(asset));
            }
        }
    }

    return info;
}

} // namespace

std::optional<UpdateChecker::ReleaseInfo> UpdateChecker::GetLatestReleaseInfo(
    bool include_prereleases) {
    constexpr auto update_check_url = "https://api.github.com";
    const std::string base_path = fmt::format("/repos/{}/{}", RepoOwner, RepoName);

    try {
        if (include_prereleases) {
            // /releases returns all releases (including prereleases), newest first.
            const auto releases_path = base_path + "/releases";
            const auto releases_response = GetResponse(update_check_url, releases_path);
            if (!releases_response) {
                return {};
            }

            const auto releases_json = nlohmann::json::parse(releases_response.value());
            if (!releases_json.is_array() || releases_json.empty()) {
                return {};
            }

            // The first entry is the most recently published release or prerelease.
            return ParseRelease(releases_json.at(0));
        } else {
            // /releases/latest only ever returns the latest *stable* (non-prerelease,
            // non-draft) release.
            const auto latest_path = base_path + "/releases/latest";
            const auto response = GetResponse(update_check_url, latest_path);
            if (!response) {
                return {};
            }

            return ParseRelease(nlohmann::json::parse(response.value()));
        }
    } catch (nlohmann::detail::exception& e) {
        LOG_ERROR(Frontend, "Parsing JSON response from {}{} failed during update check: {}",
                  update_check_url, base_path, e.what());
        return {};
    }
}

std::optional<std::string> UpdateChecker::GetLatestRelease(bool include_prereleases) {
    const auto release_info = GetLatestReleaseInfo(include_prereleases);
    if (!release_info || release_info->tag_name.empty()) {
        return {};
    }
    return release_info->tag_name;
}
