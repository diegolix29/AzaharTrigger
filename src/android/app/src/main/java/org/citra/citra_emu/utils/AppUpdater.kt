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

    private fun extractBuildVersionFromAsset(assetName: String): String {
        var versionStart = -1
        for (i in assetName.indices.reversed()) {
            if (assetName[i].isDigit()) {
                versionStart = i
            } else if (versionStart != -1 && !assetName[i].isDigit() && 
                       assetName[i] != '.' && assetName[i] != '-') {
                break
            }
        }
        
        if (versionStart == -1) {
            return ""
        }
        
        val version = StringBuilder()
        for (i in versionStart until assetName.length) {
            if (assetName[i].isDigit() || assetName[i] == '.' || assetName[i] == '-') {
                version.append(assetName[i])
            } else {
                break
            }
        }
        
        return version.toString()
    }

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

    suspend fun checkForUpdate(includePrereleases: Boolean = false): UpdateInfo? =
        withContext(Dispatchers.IO) {
            try {
                val release = fetchLatestRelease(includePrereleases) ?: return@withContext null
                if (release.tag_name.isBlank()) {
                    return@withContext null
                }

                // Compare build versions instead of git tags to avoid conflicts across platforms
                val apkAsset = release.assets.firstOrNull {
                    it.name.contains("android") && it.name.endsWith(".apk")
                } ?: return@withContext null

                val latestBuildVersion = extractBuildVersionFromAsset(apkAsset.name)
                if (latestBuildVersion.isEmpty()) {
                    Log.w(TAG, "Failed to extract build version from asset: ${apkAsset.name}")
                    return@withContext null
                }

                val currentBuildVersion = sanitizeVersion(BuildConfig.VERSION_NAME)
                Log.i(TAG, "Current build version: $currentBuildVersion, Latest build version: $latestBuildVersion")

                if (latestBuildVersion == currentBuildVersion) {
                    Log.i(TAG, "Already on latest build version: $currentBuildVersion")
                    return@withContext null
                }

                if (!isVersionNewer(currentBuildVersion, latestBuildVersion)) {
                    Log.i(TAG, "No update needed (latest build is not newer than current)")
                    return@withContext null
                }

                Log.i(TAG, "Update available: $currentBuildVersion -> $latestBuildVersion")
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