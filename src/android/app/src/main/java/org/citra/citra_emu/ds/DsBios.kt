// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.documentfile.provider.DocumentFile
import androidx.preference.PreferenceManager
import java.io.File
import java.util.Locale

/**
 * BIOS, firmware and NAND dumps picked with the system file picker.
 *
 * The DS core reads them with stdio, which can't open documents from the
 * picker, so each one is copied into `<DS dir>/bios/` and its preference points
 * at the copy. Same file names melonDS uses, so a folder of melonDS dumps can be
 * imported in one go.
 */
object DsBios {
    data class Slot(
        val key: String,
        val fileName: String,
        /** Accepted sizes in bytes; empty = anything over [minSize]. */
        val sizes: Set<Long>,
        val minSize: Long = 0,
        /** Lower-case names this dump commonly has, besides [fileName]. */
        val aliases: List<String> = emptyList()
    )

    private const val KB = 1024L

    val SLOTS = listOf(
        Slot(DsSettings.KEY_DS_BIOS9, "bios9.bin", setOf(4 * KB), aliases = listOf("biosnds9.rom", "bios_arm9.bin")),
        Slot(DsSettings.KEY_DS_BIOS7, "bios7.bin", setOf(16 * KB), aliases = listOf("biosnds7.rom", "bios_arm7.bin")),
        Slot(DsSettings.KEY_DS_FIRMWARE, "firmware.bin", setOf(128 * KB, 256 * KB, 512 * KB), aliases = listOf("fw.bin", "firmware.rom")),
        Slot(DsSettings.KEY_DSI_BIOS9, "dsi_bios9.bin", setOf(64 * KB), aliases = listOf("biosdsi9.rom", "bios9i.bin")),
        Slot(DsSettings.KEY_DSI_BIOS7, "dsi_bios7.bin", setOf(64 * KB), aliases = listOf("biosdsi7.rom", "bios7i.bin")),
        Slot(DsSettings.KEY_DSI_FIRMWARE, "dsi_firmware.bin", setOf(128 * KB), aliases = listOf("dsi_fw.bin")),
        Slot(DsSettings.KEY_DSI_NAND, "dsi_nand.bin", emptySet(), minSize = 200 * KB * KB, aliases = listOf("nand.bin", "nand_dsi.bin"))
    )

    fun slot(key: String): Slot? = SLOTS.firstOrNull { it.key == key }

    private val biosDir: File
        get() = File(DsPaths.dataDir, "bios").apply { mkdirs() }

    fun sizeLooksRight(slot: Slot, size: Long): Boolean =
        if (slot.sizes.isEmpty()) size >= slot.minSize else size in slot.sizes

    sealed class Result {
        data class Copied(val path: String, val sizeOk: Boolean) : Result()
        data class Failed(val message: String) : Result()
    }

    /** Copies [uri] into the BIOS folder and points [slot]'s preference at it. Blocking. */
    fun import(context: Context, slot: Slot, uri: Uri): Result {
        val target = File(biosDir, slot.fileName)
        val temp = File(biosDir, slot.fileName + ".part")
        return try {
            val input = context.contentResolver.openInputStream(uri)
                ?: return Result.Failed("cannot open the file")
            input.use { inp -> temp.outputStream().use { out -> inp.copyTo(out, 1 shl 20) } }
            if (target.exists()) target.delete()
            if (!temp.renameTo(target)) {
                temp.copyTo(target, overwrite = true)
                temp.delete()
            }
            PreferenceManager.getDefaultSharedPreferences(context).edit()
                .putString(slot.key, target.absolutePath).apply()
            Result.Copied(target.absolutePath, sizeLooksRight(slot, target.length()))
        } catch (e: Exception) {
            temp.delete()
            Result.Failed(e.message ?: e.javaClass.simpleName)
        }
    }

    fun clear(context: Context, slot: Slot) {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        val current = prefs.getString(slot.key, "").orEmpty()
        // Only delete our own copy, never a file the user pointed at elsewhere.
        if (current.isNotEmpty() && File(current).parentFile == biosDir) File(current).delete()
        prefs.edit().putString(slot.key, "").apply()
    }

    fun displayName(context: Context, uri: Uri): String? = runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    }.getOrNull()

    /**
     * Matches the files of a picked folder to slots: by name first, then (for the
     * DS dumps, whose sizes are unique) by size. Returns slot -> document.
     */
    fun matchFolder(context: Context, tree: Uri): Map<Slot, DocumentFile> {
        val dir = DocumentFile.fromTreeUri(context, tree) ?: return emptyMap()
        val files = dir.listFiles().filter { it.isFile }
        val found = linkedMapOf<Slot, DocumentFile>()
        for (slot in SLOTS) {
            val names = (listOf(slot.fileName) + slot.aliases).map { it.lowercase(Locale.ROOT) }
            files.firstOrNull { f -> f.name?.lowercase(Locale.ROOT) in names }?.let { found[slot] = it }
        }
        // Unique sizes: ARM9 4 KiB, ARM7 16 KiB. DSi BIOSes share a size, so names only.
        for (key in listOf(DsSettings.KEY_DS_BIOS9, DsSettings.KEY_DS_BIOS7)) {
            val slot = slot(key) ?: continue
            if (slot in found) continue
            files.filter { it !in found.values && it.length() in slot.sizes }
                .singleOrNull()?.let { found[slot] = it }
        }
        return found
    }
}
