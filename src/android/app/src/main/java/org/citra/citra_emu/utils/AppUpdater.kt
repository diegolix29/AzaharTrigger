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

/**
 * Checks the GitHub repository this build was produced from for newer
 * releases, and (if one is found) picks out the Android APK attached to it.
 *
 * This intentionally mirrors the logic in src/citra_qt/update_checker.cpp /
 * src/citra_qt/updater/self_updater.cpp used by the desktop frontend, so
 * that both frontends agree on what "a newer release" means.
 *
 * Not used at all in the googlePlay flavor; see [BuildUtil.isGooglePlayBuild].
 */
object AppUpdater {
    // Keep this in sync with UpdateChecker::RepoOwner / RepoName in
    // src/citra_qt/update_checker.h.
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

    /**
     * Parses a version string like "v1.2.3" (or "1.2.3", "1.2.3-rc1", a bare
     * git hash, etc.) into its numeric dot-separated components. Any leading
     * non-digit characters (e.g. a "v" prefix) are skipped first. Non-numeric
     * trailing garbage (e.g. "-rc1") just stops the parse at that component.
     */
    private fun parseVersionComponents(version: String): List<Int> {
        val parts = mutableListOf<Int>()
        val start = version.indexOfFirst { it.isDigit() }
        if (start == -1) return parts
        val ss = version.substring(start)
        for (segment in ss.split('.')) {
            try {
                parts.add(segment.toInt())
            } catch (e: NumberFormatException) {
                break
            }
        }
        return parts
    }

    /**
     * Returns true if `latest` is a strictly newer version than `current`,
     * comparing components numerically (major, then minor, then patch, ...)
     * instead of only an exact string match. Missing trailing components are
     * treated as 0, so "1.2" < "1.2.1".
     */
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

    /**
     * Returns information about the newest available update, or null if the
     * current build is already up to date, no matching release/asset could
     * be found, or the check failed (e.g. no network connection).
     */
    suspend fun checkForUpdate(includePrereleases: Boolean = false): UpdateInfo? =
        withContext(Dispatchers.IO) {
            try {
                val release = fetchLatestRelease(includePrereleases) ?: return@withContext null
                if (release.tag_name.isBlank() || release.tag_name == BuildConfig.VERSION_NAME) {
                    return@withContext null
                }

                // Previously this only checked for an exact string match against
                // VERSION_NAME, with no fallback. If the running build's embedded
                // version string isn't literally identical to the latest release tag
                // (e.g. it's a raw commit hash because it wasn't built from a
                // tagged ref), this used to report an "update" unconditionally,
                // even when already on the newest release. Gate on an actual
                // version comparison instead.
                if (!isVersionNewer(BuildConfig.VERSION_NAME, release.tag_name)) {
                    return@withContext null
                }

                val apkAsset = release.assets.firstOrNull {
                    it.name.contains("android") && it.name.endsWith(".apk")
                } ?: return@withContext null

                UpdateInfo(release.tag_name, release.html_url, apkAsset)
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
