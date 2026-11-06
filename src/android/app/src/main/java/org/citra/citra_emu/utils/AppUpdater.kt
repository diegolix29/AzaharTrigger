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

    // A release build's VERSION_NAME is "X.Y.Z" (optionally "vX.Y.Z" / with a suffix).
    // Anything else - e.g. a bare git hash from a dev build - is not comparable.
    private fun isVersionedBuild(version: String): Boolean =
        Regex("""^v?\d+\.\d+""").containsMatchIn(version)

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
     * Returns the latest release if it is newer than this build, otherwise null.
     *
     * The release tag ("vX.Y.Z") is the only source of truth: the release workflow publishes
     * only on a tag push and bakes that tag into every build's VERSION_NAME. Asset names are used
     * solely to pick which APK to download.
     *
     * Builds without a real version (local/dev builds, whose VERSION_NAME is a git hash) can't be
     * compared. With [manualCheck] false they never prompt; a manual check still offers the
     * latest release.
     */
    suspend fun checkForUpdate(
        includePrereleases: Boolean = false,
        manualCheck: Boolean = true
    ): UpdateInfo? =
        withContext(Dispatchers.IO) {
            try {
                val release = fetchLatestRelease(includePrereleases) ?: return@withContext null
                if (release.tag_name.isBlank()) {
                    return@withContext null
                }

                // Prefer the APK built for this tag; a release may also carry stale
                // "<date>-<hash>" uploads from older builds.
                val apks = release.assets.filter {
                    it.name.contains("android") && it.name.endsWith(".apk")
                }
                val apkAsset = apks.firstOrNull { it.name.contains(release.tag_name) }
                    ?: apks.firstOrNull()
                    ?: return@withContext null

                val latestVersion = release.tag_name
                val currentVersion = BuildConfig.VERSION_NAME
                Log.i(TAG, "Current version: $currentVersion, latest release: $latestVersion")

                if (!isVersionedBuild(currentVersion)) {
                    if (!manualCheck) {
                        Log.i(TAG, "Unversioned build, skipping automatic update prompt")
                        return@withContext null
                    }
                    return@withContext UpdateInfo(release.tag_name, release.html_url, apkAsset)
                }

                if (!isVersionNewer(currentVersion, latestVersion)) {
                    Log.i(TAG, "No update needed (already on latest or newer version)")
                    return@withContext null
                }

                Log.i(TAG, "Update available: $currentVersion -> $latestVersion (${apkAsset.name})")
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