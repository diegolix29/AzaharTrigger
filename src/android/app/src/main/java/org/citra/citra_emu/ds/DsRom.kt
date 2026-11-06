// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.net.Uri
import android.os.ParcelFileDescriptor
import androidx.core.net.toUri
import java.io.Closeable
import java.io.File
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.utils.FileUtil
import org.citra.citra_emu.utils.Log

/**
 * Opening .nds files for the DS core.
 *
 * The core reads ROMs with plain stdio, but Android hands games over as storage
 * access framework documents as often as real paths. A document is opened here
 * and given to the core as `/proc/self/fd/<fd>`, which fopen() accepts; the
 * descriptor has to stay open for as long as the core may read from it, hence
 * [Handle].
 *
 * Paths reaching this object come in all the shapes Azahar uses:
 *  - `/storage/...`           a real path;
 *  - `!/storage/...`          a real path Azahar derived from a document tree
 *                             (see TranslateFilePath in common/android_utils.cpp);
 *  - `content://...`          a storage access framework document;
 *  - `fd://<n>`               a descriptor another app already opened for us;
 *  - `Some/Relative/Path`     relative to the Azahar user directory.
 */
object DsRom {
    /** File extensions the DS core runs, matching DS::FileExtensions() on desktop. */
    val extensions: Set<String> = setOf("nds", "srl", "dsi", "ids")

    fun isDsFile(path: String): Boolean {
        val name = path.substringAfterLast('/').substringAfterLast('\\')
        val extension = name.substringAfterLast('.', "").lowercase()
        return extension in extensions
    }

    /** A ROM the core can open, plus whatever has to be released afterwards. */
    class Handle(
        /** What to hand to the core. */
        val path: String,
        private val fd: ParcelFileDescriptor?
    ) : Closeable {
        override fun close() {
            try {
                fd?.close()
            } catch (e: Exception) {
                Log.warning("[DS] could not close the ROM descriptor: ${e.message}")
            }
        }
    }

    /**
     * Resolves [path] into something the core can read, falling back to
     * [documentUri] (a `content://` string, usually a game's `description`) when
     * the path itself is not readable. Returns null if neither can be opened.
     */
    fun open(path: String, documentUri: String? = null): Handle? {
        // Citra's Log is native, and so is everything below this point.
        DsNative.ensureLoaded()
        realPath(path)?.let { return Handle(it, null) }

        val uri = toContentUri(path)
            ?: documentUri?.let { toContentUri(it) }
            ?: run {
                Log.error("[DS] cannot resolve $path")
                return null
            }

        return try {
            val fd = CitraApplication.appContext.contentResolver.openFileDescriptor(uri, "r")
            if (fd == null) {
                Log.error("[DS] no file descriptor for $uri")
                null
            } else {
                Handle("/proc/self/fd/${fd.fd}", fd)
            }
        } catch (e: Exception) {
            Log.error("[DS] could not open $uri: ${e.message}")
            null
        }
    }

    /** Reads a ROM's banner without keeping it open. Returns null if it is not a DS ROM. */
    fun probe(path: String, documentUri: String? = null): DsRomInfo? {
        // Whatever goes wrong here, the game still belongs in the list, so nothing
        // from this is allowed to escape into the scan.
        return try {
            open(path, documentUri)?.use { DsRomInfo.probe(it.path) }
        } catch (e: Throwable) {
            Log.error("[DS] could not read $path: $e")
            null
        }
    }

    /**
     * File name without its extension, used to name saves and savestates. Stays
     * stable for a given ROM, since saves are looked up by it.
     */
    fun baseName(path: String, fallback: String? = null): String {
        val name = when {
            path.startsWith("content://") -> runCatching {
                FileUtil.getFilename(path.toUri())
            }.getOrDefault("")
            else -> path.trimStart('!').substringAfterLast('/')
        }.ifEmpty { fallback ?: path.substringAfterLast('/') }
        return name.substringBeforeLast('.').ifEmpty { "game" }
    }

    /** The path as a readable real file, or null if it is not one. */
    private fun realPath(path: String): String? {
        val candidate = when {
            path.isEmpty() -> return null
            // Azahar's marker for a path that is already native.
            path.startsWith("!") -> path.substring(1)
            path.startsWith("/") -> path
            // A descriptor somebody else owns: readable through procfs, and not ours to close.
            path.startsWith("fd://") -> {
                val fd = path.removePrefix("fd://").toIntOrNull() ?: return null
                "/proc/self/fd/$fd"
            }
            else -> return null
        }
        return if (File(candidate).canRead()) candidate else null
    }

    private fun toContentUri(path: String): Uri? = try {
        when {
            path.isEmpty() -> null
            path.startsWith("content://") -> path.toUri()
            // Real paths and descriptors are handled by realPath().
            path.startsWith("/") || path.startsWith("!") || path.startsWith("fd://") -> null
            // A path relative to the Azahar user directory.
            else -> CitraApplication.documentsTree.getUri(path)
        }
    } catch (e: Exception) {
        Log.error("[DS] could not resolve $path: ${e.message}")
        null
    }
}
