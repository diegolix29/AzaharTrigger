// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.util.Log
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import org.citra.citra_emu.BuildConfig

object AppUpdater {
    private const val REPO_OWNER = "diegolix29"
    private const val REPO_NAME = "AzaharTrigger"

    private const val TAG = "AppUpdater"

    private val json = Json { ignoreUnknownKeys = true }

    @Serializable
    data class ReleaseAsset(
        val name: String,
        val browser_download_url: String,
        val size: Long = 0
    )

    @Serializable
    data class Release(
        val tag_name: String,
        val html_url: String = "",
        val prerelease: Boolean = false,
        val assets: List<ReleaseAsset> = emptyList()
    )

    data class UpdateInfo(
        val tagName: String,
        val htmlUrl: String,
        val apkAsset: ReleaseAsset
    )

    private fun parseVersionComponents(version: String): List<Int> {
        val parts = mutableListOf<Int>()
        val start = version.indexOfFirst { it.isDigit() }
        if (start == -1) return parts
        val ss = version.substring(start)
        for (segment in ss.split('.', '-')) {
            try {
                parts.add(segment.toInt())
            } catch (e: NumberFormatException) {
                break
            }
        }
        return parts
    }

    private fun sanitizeVersion(version: String): String =
        Regex("^\\d+(?:\\.\\d+)*").find(version)?.value ?: version

    private fun isVersionNewer(current: String, latest: String): Boolean {
        val currentParts = parseVersionComponents(current)
        val latestParts = parseVersionComponents(latest)
        val count = maxOf(currentParts.size, latestParts.size)
        for (i in 0 until count) {
            val c = if (i < currentParts.size) currentParts[i] else 0
            val l = if (i < latestParts.size) latestParts[i] else 0
            if (c != l) {
                return c < l
            }
        }
        return false
    }

    private fun extractVersionFromAssetName(assetName: String): String? {
        // Try to extract version from asset name like "AzaharTrigger-1.2.3-android.apk"
        val versionPattern = Regex("(\\d+\\.\\d+\\.\\d+)")
        val match = versionPattern.find(assetName)
        return match?.value
    }

    suspend fun checkForUpdate(includePrereleases: Boolean = false): UpdateInfo? =
        withContext(Dispatchers.IO) {
            try {
                val release = fetchLatestRelease(includePrereleases) ?: return@withContext null
                if (release.tag_name.isBlank()) {
                    return@withContext null
                }

                // The asset filename is only used to pick which file to download.
                // It must NOT be used to determine the version: dev/nightly builds
                // are named with a build date + commit hash (e.g. "...-20260715-a1b2c3d.apk"),
                // and reverse-parsing digits out of that tail produces garbage
                // (e.g. it could pick up just "3" or "123" from inside the hash),
                // which caused bogus update-available / no-update-needed results.
                val apkAsset = release.assets.firstOrNull {
                    it.name.contains("android") && it.name.endsWith(".apk")
                } ?: return@withContext null

                // For stable releases, release.tag_name is always a clean "vX.Y.Z" 
                // produced by the release workflow itself, regardless of what the asset is named,
                // so it's a stable, unambiguous source of truth for comparison.
                // For nightly/dev builds, we need to handle different formats.
                val latestVersion = sanitizeVersion(release.tag_name.removePrefix("v"))
                if (latestVersion.isEmpty()) {
                    Log.w(TAG, "Failed to parse version from release tag: ${release.tag_name}")
                    return@withContext null
                }

                val currentVersion = sanitizeVersion(BuildConfig.VERSION_NAME)
                Log.i(TAG, "Current version: $currentVersion, Latest version: $latestVersion")
                Log.i(TAG, "Release tag: ${release.tag_name}, Asset: ${apkAsset.name}")

                // For nightly/dev builds, also check if the asset name contains a different version
                // This handles cases where the tag might not reflect the actual build version
                val assetVersion = extractVersionFromAssetName(apkAsset.name)
                if (assetVersion != null && assetVersion != latestVersion) {
                    Log.i(TAG, "Asset version differs from tag: $assetVersion vs $latestVersion")
                    // Use the asset version if it's more specific
                    val comparisonVersion = if (isVersionNewer(latestVersion, assetVersion)) {
                        assetVersion
                    } else {
                        latestVersion
                    }
                    
                    if (!isVersionNewer(currentVersion, comparisonVersion)) {
                        Log.i(TAG, "No update needed (already on latest or newer version)")
                        return@withContext null
                    }
                    
                    Log.i(TAG, "Update available: $currentVersion -> $comparisonVersion")
                    UpdateInfo(release.tag_name, release.html_url, apkAsset)
                } else {
                    if (!isVersionNewer(currentVersion, latestVersion)) {
                        Log.i(TAG, "No update needed (already on latest or newer version)")
                        return@withContext null
                    }

                    Log.i(TAG, "Update available: $currentVersion -> $latestVersion")
                    UpdateInfo(release.tag_name, release.html_url, apkAsset)
                }
            } catch (e: Exception) {
                Log.w(TAG, "Update check failed: ${e.message}")
                null
            }
        }

    private fun fetchLatestRelease(includePrereleases: Boolean): Release? {
        val path = if (includePrereleases) "releases" else "releases/latest"
        val url = URL("https://api.github.com/repos/$REPO_OWNER/$REPO_NAME/$path")
        val connection = url.openConnection() as HttpURLConnection
        return try {
            connection.requestMethod = "GET"
            connection.setRequestProperty("Accept", "application/vnd.github+json")
            connection.setRequestProperty("User-Agent", "$REPO_NAME-updater")
            connection.connectTimeout = 15_000
            connection.readTimeout = 15_000

            if (connection.responseCode !in 200..299) {
                Log.w(TAG, "GitHub API returned ${connection.responseCode}")
                return null
            }

            val body = connection.inputStream.bufferedReader().use { it.readText() }
            if (includePrereleases) {
                // /releases returns an array, newest first.
                val releases = json.decodeFromString<List<Release>>(body)
                releases.firstOrNull()
            } else {
                json.decodeFromString<Release>(body)
            }
        } finally {
            connection.disconnect()
        }
    }
}