// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import java.io.File
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.utils.Log

/**
 * Where the DS core keeps its files. Mirrors the desktop frontend's layout
 * (`<user dir>/ds/{saves,states}`), so a user directory shared between the two
 * holds one set of saves.
 *
 * The DS core opens everything with plain stdio, so these have to be real
 * filesystem paths. If Azahar's user directory is not usable as one (it can be a
 * SAF tree we only reach through the documents provider), the app's own external
 * files directory is used instead.
 */
object DsPaths {
    private const val DS_DIR = "ds"

    private val root: File by lazy { resolveRoot() }

    /** `<root>/ds/`, with a trailing separator: what azds_config.data_dir wants. */
    val dataDir: String get() = ensure(root).absolutePath + File.separator

    val savesDir: String get() = ensure(File(root, "saves")).absolutePath + File.separator

    val statesDir: String get() = ensure(File(root, "states")).absolutePath + File.separator

    /** Cartridge save file for [romName] (a file name without its extension). */
    fun savePath(romName: String): String = savesDir + romName + ".sav"

    /** Savestate slot file, using melonDS's own `.mlN` naming. */
    fun statePath(romName: String, slot: Int): String = statesDir + romName + ".ml" + slot

    private fun resolveRoot(): File {
        val userDir = try {
            NativeLibrary.getUserDirectory()
        } catch (e: Exception) {
            Log.error("[DS] could not read the user directory: ${e.message}")
            ""
        }

        if (userDir.isNotEmpty() && userDir.startsWith("/")) {
            val candidate = File(userDir, DS_DIR)
            if (ensure(candidate).isDirectory && candidate.canWrite()) {
                return candidate
            }
            Log.warning("[DS] $candidate is not writable, falling back to app storage")
        }

        // Always a real, writable path, and no storage permission needed.
        val fallback = File(
            CitraApplication.appContext.getExternalFilesDir(null)
                ?: CitraApplication.appContext.filesDir,
            DS_DIR
        )
        return ensure(fallback)
    }

    private fun ensure(dir: File): File {
        if (!dir.isDirectory && !dir.mkdirs()) {
            Log.warning("[DS] could not create $dir")
        }
        return dir
    }
}
