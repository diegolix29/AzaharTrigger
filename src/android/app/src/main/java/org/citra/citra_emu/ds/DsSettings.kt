// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication

/**
 * Frontend settings of the DS core. The desktop frontend keeps these in
 * `azalon_ds.ini`; on Android they live in the shared preferences, under the
 * `ds_` prefix, and are editable from [DsSettingsActivity].
 */
class DsSettings {
    /** 0 = DS, 1 = DSi. */
    var consoleType: Int = 0

    /** Skip the firmware menu and boot the cartridge directly. */
    var directBoot: Boolean = true
    var jit: Boolean = true
    var threadedRenderer: Boolean = true

    var nickname: String = "Azalon"

    /** 0 JP, 1 EN, 2 FR, 3 DE, 4 IT, 5 ES, 6 ZH, 7 KO. */
    var language: Int = 1
    var favoriteColor: Int = 0

    // Empty = built-in FreeBIOS and generated firmware (DS mode only).
    var dsBios9: String = ""
    var dsBios7: String = ""
    var dsFirmware: String = ""

    // DSi mode needs all of these.
    var dsiBios9: String = ""
    var dsiBios7: String = ""
    var dsiFirmware: String = ""
    var dsiNand: String = ""

    /** Virtual SD card for homebrew. */
    var dldiEnable: Boolean = true
    var dldiSyncFolder: String = ""

    /** azds_mp_mode: -1 off, 0 local, 1 LAN. */
    var wirelessMode: Int = 0
    var internet: Boolean = true

    // Frontend-only: how the two screens are arranged.
    var layout: DsScreenLayout = DsScreenLayout.VERTICAL
    var swapScreens: Boolean = false
    var showOverlay: Boolean = true

    companion object {
        const val KEY_CONSOLE_TYPE = "ds_console_type"
        const val KEY_DIRECT_BOOT = "ds_direct_boot"
        const val KEY_JIT = "ds_jit"
        const val KEY_THREADED_RENDERER = "ds_threaded_renderer"
        const val KEY_NICKNAME = "ds_nickname"
        const val KEY_LANGUAGE = "ds_language"
        const val KEY_FAVORITE_COLOR = "ds_favorite_color"
        const val KEY_DS_BIOS9 = "ds_bios9"
        const val KEY_DS_BIOS7 = "ds_bios7"
        const val KEY_DS_FIRMWARE = "ds_firmware"
        const val KEY_DSI_BIOS9 = "ds_dsi_bios9"
        const val KEY_DSI_BIOS7 = "ds_dsi_bios7"
        const val KEY_DSI_FIRMWARE = "ds_dsi_firmware"
        const val KEY_DSI_NAND = "ds_dsi_nand"
        const val KEY_DLDI_ENABLE = "ds_dldi_enable"
        const val KEY_DLDI_SYNC_FOLDER = "ds_dldi_sync_folder"
        const val KEY_WIRELESS_MODE = "ds_wireless_mode"
        const val KEY_INTERNET = "ds_internet"
        const val KEY_LAYOUT = "ds_layout"
        const val KEY_SWAP_SCREENS = "ds_swap_screens"
        const val KEY_SHOW_OVERLAY = "ds_show_overlay"

        /** Reads the current settings. Cheap enough to call when a game starts. */
        fun load(): DsSettings {
            val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
            val defaults = DsSettings()
            return DsSettings().apply {
                consoleType = prefs.intFrom(KEY_CONSOLE_TYPE, defaults.consoleType)
                directBoot = prefs.getBoolean(KEY_DIRECT_BOOT, defaults.directBoot)
                jit = prefs.getBoolean(KEY_JIT, defaults.jit)
                threadedRenderer =
                    prefs.getBoolean(KEY_THREADED_RENDERER, defaults.threadedRenderer)
                nickname = prefs.getString(KEY_NICKNAME, defaults.nickname)!!
                language = prefs.intFrom(KEY_LANGUAGE, defaults.language)
                favoriteColor = prefs.intFrom(KEY_FAVORITE_COLOR, defaults.favoriteColor)
                dsBios9 = prefs.getString(KEY_DS_BIOS9, "")!!
                dsBios7 = prefs.getString(KEY_DS_BIOS7, "")!!
                dsFirmware = prefs.getString(KEY_DS_FIRMWARE, "")!!
                dsiBios9 = prefs.getString(KEY_DSI_BIOS9, "")!!
                dsiBios7 = prefs.getString(KEY_DSI_BIOS7, "")!!
                dsiFirmware = prefs.getString(KEY_DSI_FIRMWARE, "")!!
                dsiNand = prefs.getString(KEY_DSI_NAND, "")!!
                dldiEnable = prefs.getBoolean(KEY_DLDI_ENABLE, defaults.dldiEnable)
                dldiSyncFolder = prefs.getString(KEY_DLDI_SYNC_FOLDER, "")!!
                wirelessMode = prefs.intFrom(KEY_WIRELESS_MODE, defaults.wirelessMode)
                internet = prefs.getBoolean(KEY_INTERNET, defaults.internet)
                layout = DsScreenLayout.fromInt(prefs.intFrom(KEY_LAYOUT, defaults.layout.value))
                swapScreens = prefs.getBoolean(KEY_SWAP_SCREENS, defaults.swapScreens)
                showOverlay = prefs.getBoolean(KEY_SHOW_OVERLAY, defaults.showOverlay)
            }
        }

        fun saveLayout(layout: DsScreenLayout, swapScreens: Boolean) {
            PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
                .edit()
                .putString(KEY_LAYOUT, layout.value.toString())
                .putBoolean(KEY_SWAP_SCREENS, swapScreens)
                .apply()
        }

        fun saveShowOverlay(show: Boolean) {
            PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
                .edit()
                .putBoolean(KEY_SHOW_OVERLAY, show)
                .apply()
        }

        /**
         * ListPreference stores integers as strings; this reads either form so the
         * settings screen and any older values both work.
         */
        private fun android.content.SharedPreferences.intFrom(key: String, default: Int): Int =
            try {
                getString(key, null)?.toIntOrNull() ?: default
            } catch (e: ClassCastException) {
                getInt(key, default)
            }
    }
}

/** Screen arrangement, matching DS::ScreenLayout in the desktop frontend. */
enum class DsScreenLayout(val value: Int) {
    VERTICAL(0),
    HORIZONTAL(1),
    TOP_ONLY(2),
    BOTTOM_ONLY(3);

    companion object {
        fun fromInt(value: Int): DsScreenLayout =
            entries.find { it.value == value } ?: VERTICAL
    }
}
