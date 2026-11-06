// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.utils

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.provider.DocumentsContract
import androidx.core.net.toUri
import androidx.core.os.BundleCompat
import androidx.preference.PreferenceManager
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.ds.DsRom
import org.citra.citra_emu.model.CheapDocument
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.model.GameInfo

object GameHelper {
    /** First games folder only; kept in sync with [KEY_GAME_PATHS] for older code paths. */
    const val KEY_GAME_PATH = "game_path"

    /** Every games folder the user added, newline separated, in the order they were added. */
    const val KEY_GAME_PATHS = "game_paths"
    const val KEY_GAMES = "Games"

    // Since the game val in GetGame is tied to the JNI we need to cache the game list in order to use it elsewhere
    var cachedGameList = mutableListOf<Game>()
    private lateinit var preferences: SharedPreferences

    private val prefs: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)

    /** The tree URIs of all games folders. Falls back to the single legacy folder. */
    fun getGameDirs(): List<String> {
        val stored = prefs.getString(KEY_GAME_PATHS, null)
        if (stored != null) {
            return stored.split('\n').filter { it.isNotBlank() }
        }
        val legacy = prefs.getString(KEY_GAME_PATH, "") ?: ""
        return if (legacy.isEmpty()) emptyList() else listOf(legacy)
    }

    private fun saveGameDirs(dirs: List<String>) {
        prefs.edit()
            .putString(KEY_GAME_PATHS, dirs.joinToString("\n"))
            .putString(KEY_GAME_PATH, dirs.firstOrNull() ?: "")
            .apply()
    }

    /**
     * Adds a games folder and keeps read access to it across reboots.
     * Returns false when the folder was already in the list.
     */
    fun addGameDir(context: Context, uri: Uri): Boolean {
        context.contentResolver.takePersistableUriPermission(
            uri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION
        )
        val dirs = getGameDirs()
        val value = uri.toString()
        if (dirs.contains(value)) {
            return false
        }
        saveGameDirs(dirs + value)
        return true
    }

    fun removeGameDir(context: Context, dir: String) {
        saveGameDirs(getGameDirs().filter { it != dir })

        // Give the folder's permission back, unless it is also the user directory.
        if (dir == prefs.getString(PermissionsHandler.CITRA_DIRECTORY, "")) {
            return
        }
        try {
            context.contentResolver.releasePersistableUriPermission(
                Uri.parse(dir),
                Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        } catch (e: Exception) {
            Log.warning("[GameHelper] Could not release permission for $dir: ${e.message}")
        }
    }

    /** Short readable name for a folder, e.g. "primary:Games/3DS". */
    fun describeGameDir(dir: String): String {
        val uri = Uri.parse(dir)
        return try {
            DocumentsContract.getTreeDocumentId(uri)
        } catch (e: Exception) {
            uri.path ?: dir
        }
    }

    fun describeGameDirs(): String = getGameDirs().joinToString("\n") { describeGameDir(it) }

    fun getGames(): List<Game> {
        val games = mutableListOf<Game>()
        val context = CitraApplication.appContext
        preferences = PreferenceManager.getDefaultSharedPreferences(context)

        getGameDirs().forEach { dir ->
            try {
                addGamesRecursive(games, FileUtil.listFiles(Uri.parse(dir)), 3)
            } catch (e: Exception) {
                Log.error("[GameHelper] Could not scan $dir: ${e.message}")
            }
        }
        // Overlapping folders (one inside another) would list the same file twice.
        val seen = mutableSetOf<String>()
        games.retainAll { seen.add(it.path) }

        NativeLibrary.getInstalledGamePaths().forEach {
            games.add(
                getGame(Uri.parse(it.path), isInstalled = true, addedToLibrary = true, it.mediaType)
            )
        }

        // Cache list of games found on disk
        val serializedGames = mutableSetOf<String>()
        games.forEach {
            serializedGames.add(Json.encodeToString(it))
        }
        preferences.edit()
            .remove(KEY_GAMES)
            .putStringSet(KEY_GAMES, serializedGames)
            .apply()

        cachedGameList = games.toMutableList()
        return games.toList()
    }

    private fun addGamesRecursive(
        games: MutableList<Game>,
        files: Array<CheapDocument>,
        depth: Int
    ) {
        if (depth <= 0) {
            return
        }

        files.forEach {
            // The directory listing already told us the name; only pay for a
            // per-file query when the documents provider left it out. Asking again
            // can come back empty, which used to make us skip the file silently.
            val filename = it.filename.ifEmpty { FileUtil.getFilename(it.uri) }
            if (filename.startsWith(".")) {
                return@forEach
            }
            if (it.isDirectory) {
                addGamesRecursive(games, FileUtil.listFiles(it.uri), depth - 1)
            } else {
                val extension = filename.substringAfterLast('.', "").lowercase()
                if (!Game.extensions.contains(extension)) {
                    return@forEach
                }
                // One unreadable file must not cost the user the whole library.
                try {
                    games.add(
                        getGame(
                            it.uri,
                            isInstalled = false,
                            addedToLibrary = true,
                            Game.MediaType.GAME_CARD
                        )
                    )
                } catch (e: Throwable) {
                    Log.error("[GameHelper] Skipped $filename: $e")
                }
            }
        }
    }

    fun getGame(
        uri: Uri,
        isInstalled: Boolean,
        addedToLibrary: Boolean,
        mediaType: Game.MediaType
    ): Game {
        val filePath = uri.toString()
        if (DsRom.isDsFile(FileUtil.getFilename(uri).ifEmpty { filePath })) {
            return getDsGame(uri, addedToLibrary)
        }

        var nativePath: String? = null
        var gameInfo: GameInfo?
        if (BuildUtil.isGooglePlayBuild || FileUtil.isNativePath(filePath) ||
            filePath.startsWith("!")
        ) {
            gameInfo = GameInfo(filePath)
        } else {
            nativePath = if (uri.scheme == "fd") {
                uri.toString()
            } else {
                "!" + NativeLibrary.getNativePath(uri)
            }
            gameInfo = GameInfo(nativePath)
        }

        val valid = gameInfo.isValid()
        if (!valid) {
            gameInfo = null
        }

        val isEncrypted = gameInfo?.isEncrypted() == true

        val newGame = Game(
            valid,
            (
                gameInfo?.getTitle() ?: FileUtil.getFilename(
                    uri
                )
                ).replace("[\\t\\n\\r]+".toRegex(), " "),
            filePath.replace("\n", " "),
            // TODO: This next line can be deduplicated but I don't want to right now -OS
            if (BuildUtil.isGooglePlayBuild || FileUtil.isNativePath(filePath) ||
                filePath.startsWith("!")
            ) {
                filePath
            } else {
                nativePath!!
            },
            gameInfo?.getTitleID() ?: 0,
            mediaType,
            gameInfo?.getCompany() ?: "",
            if (isEncrypted) {
                CitraApplication.appContext.getString(R.string.unsupported_encrypted)
            } else {
                gameInfo?.getRegions()
                    ?: ""
            },
            isInstalled,
            gameInfo?.isSystemTitle() ?: false,
            gameInfo?.getIsVisibleSystemTitle() ?: false,
            gameInfo?.getIsInsertable() ?: false,
            gameInfo?.getIcon(),
            gameInfo?.getFileType() ?: "",
            gameInfo?.getFileType()?.contains("(Z)") ?: false,
            if (FileUtil.isNativePath(filePath)) {
                CitraApplication.documentsTree.getFilename(filePath)
            } else {
                FileUtil.getFilename(Uri.parse(filePath))
            }
        )

        if (addedToLibrary) {
            val addedTime = preferences.getLong(newGame.keyAddedToLibraryTime, 0L)
            if (addedTime == 0L) {
                preferences.edit()
                    .putLong(newGame.keyAddedToLibraryTime, System.currentTimeMillis())
                    .apply()
            }
        }

        return newGame
    }

    /**
     * Builds a game list entry for a DS/DSi cartridge. The 3DS loaders know
     * nothing about .nds files, so the title, icon and region come from the DS
     * core's own ROM probe instead of [GameInfo].
     */
    private fun getDsGame(uri: Uri, addedToLibrary: Boolean): Game {
        val filePath = uri.toString()
        // The DS core reads files with stdio, so prefer a real path where we have one.
        val corePath = if (BuildUtil.isGooglePlayBuild || FileUtil.isNativePath(filePath) ||
            filePath.startsWith("!")
        ) {
            filePath
        } else if (uri.scheme == "fd") {
            filePath
        } else {
            "!" + NativeLibrary.getNativePath(uri)
        }

        val filename = if (FileUtil.isNativePath(filePath)) {
            CitraApplication.documentsTree.getFilename(filePath)
        } else {
            FileUtil.getFilename(uri)
        }.ifEmpty { corePath.substringAfterLast('/') }

        val info = DsRom.probe(corePath, filePath)
        if (info == null) {
            // Still listed, under its file name: the user can tell it was found.
            Log.warning("[GameHelper] No DS banner in $filename (core path: $corePath)")
        } else {
            Log.info("[GameHelper] DS game ${info.shortTitle} [${info.gameCode}] ${info.typeName}")
        }

        val newGame = Game(
            valid = info != null,
            title = (info?.shortTitle?.ifEmpty { null } ?: filename.substringBeforeLast('.'))
                .replace("[\\t\\n\\r]+".toRegex(), " "),
            description = filePath.replace("\n", " "),
            path = corePath,
            titleId = info?.programId ?: 0L,
            mediaType = Game.MediaType.GAME_CARD,
            company = "",
            regions = info?.region ?: "",
            isInstalled = false,
            isSystemTitle = false,
            isVisibleSystemTitle = false,
            isInsertable = false,
            icon = info?.icon,
            fileType = info?.typeName ?: "NDS",
            isCompressed = false,
            filename = filename
        )

        if (addedToLibrary) {
            val addedTime = preferences.getLong(newGame.keyAddedToLibraryTime, 0L)
            if (addedTime == 0L) {
                preferences.edit()
                    .putLong(newGame.keyAddedToLibraryTime, System.currentTimeMillis())
                    .apply()
            }
        }

        return newGame
    }

    fun getGameFromIntent(context: Context, intent: Intent): Game? {
        val parcelable = try {
            intent.extras?.let { BundleCompat.getParcelable(it, "game", Game::class.java) }
        } catch (e: Exception) {
            Log.error("[GameHelper] Failed to retrieve game data: ${e.message}")
            null
        }
        if (parcelable != null) return parcelable

        val uri = getLaunchUri(intent) ?: return null
        return try {
            if (BuildUtil.isGooglePlayBuild || uri.toString().startsWith("!")) {
                getGame(
                    uri,
                    isInstalled = false,
                    addedToLibrary = false,
                    mediaType = Game.MediaType.GAME_CARD
                )
            } else {
                context.contentResolver.openFileDescriptor(uri, "r")?.use { fd ->
                    getGame(
                        "fd://${fd.fd}".toUri(),
                        isInstalled = false,
                        addedToLibrary = false,
                        mediaType = Game.MediaType.GAME_CARD
                    )
                }
            }
        } catch (e: Exception) {
            Log.error("[GameHelper] Failed to resolve game from intent: ${e.message}")
            null
        }
    }

    fun getLaunchUri(intent: Intent): Uri? = try {
        val game = intent.extras?.let { extras ->
            BundleCompat.getParcelable(extras, "game", Game::class.java)
        }
        if (game != null) {
            game.launchIntent.data
        } else {
            intent.data ?: intent.getStringExtra("SelectedGame")?.toUri()
        }
    } catch (e: Exception) {
        Log.error("[GameHelper] Failed to parse URI from intent: ${e.message}")
        null
    }
}
