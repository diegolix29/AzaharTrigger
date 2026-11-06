// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import androidx.annotation.Keep

/**
 * Title, game code and banner icon read straight out of a .nds header.
 * Constructed from JNI (see ds_jni.cpp); keep the constructor signature in sync.
 */
@Keep
class DsRomInfo(
    /** Banner title; several lines joined by '\n', or the header title as a fallback. */
    val title: String,
    /** 4-character cartridge game code, e.g. "ADAE". */
    val gameCode: String,
    /** 32x32 ARGB8888 banner icon, or null when the ROM has no banner. */
    val icon: IntArray?,
    val isDsiEnhanced: Boolean,
    val isDsiExclusive: Boolean,
    val isHomebrew: Boolean
) {
    /** First line of the banner title, which is what the game list shows. */
    val shortTitle: String get() = title.substringBefore('\n').trim()

    /** What the game list shows in the "type" column. */
    val typeName: String
        get() = when {
            isDsiExclusive -> "DSi"
            isDsiEnhanced -> "NDS (DSi enhanced)"
            isHomebrew -> "NDS (homebrew)"
            else -> "NDS"
        }

    /** Region name derived from the game code's fourth character. */
    val region: String
        get() = when (gameCode.getOrNull(3)) {
            'J' -> "Japan"
            'E' -> "North America"
            'P', 'D', 'F', 'I', 'S', 'X', 'Y', 'Z' -> "Europe"
            'K' -> "Korea"
            'C' -> "China"
            'U' -> "Australia"
            else -> "Region free"
        }

    /**
     * Synthetic title id: "NDS" followed by the 4-character game code, exactly as
     * the desktop frontend computes it (see DSProgramId in game_list_worker.cpp),
     * so per-game settings and play-time keys are shared between the two.
     * Never a valid 3DS title id, and stable across rescans.
     */
    val programId: Long
        get() {
            var id = 0x4E4453L shl 40
            for (i in 0 until 4) {
                val c = gameCode.getOrNull(i)?.code?.toLong() ?: 0L
                id = id or ((c and 0xFF) shl (24 - 8 * i))
            }
            return id
        }

    companion object {
        /** Reads [path] (a real filesystem path). Returns null if it is not a DS ROM. */
        @JvmStatic
        external fun probe(path: String): DsRomInfo?
    }
}
