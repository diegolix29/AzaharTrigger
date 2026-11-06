// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.view.Surface
import androidx.annotation.Keep
import org.citra.citra_emu.utils.Log

/**
 * The running DS/DSi console, as exposed by src/android/app/src/main/jni/ds.
 *
 * Only one console runs at a time, so everything here is static. All methods are
 * safe to call from the UI thread; emulation itself runs on a native thread.
 */
@Keep
object DsNative {
    /**
     * DS button bits, as understood by [setKeys]. These are the DS KEYINPUT bit
     * positions and match the AZDS_KEY_* values in azalon_ds.h.
     */
    object Key {
        const val A = 1 shl 0
        const val B = 1 shl 1
        const val SELECT = 1 shl 2
        const val START = 1 shl 3
        const val RIGHT = 1 shl 4
        const val LEFT = 1 shl 5
        const val UP = 1 shl 6
        const val DOWN = 1 shl 7
        const val R = 1 shl 8
        const val L = 1 shl 9
        const val X = 1 shl 10
        const val Y = 1 shl 11
    }

    const val SCREEN_TOP = 0
    const val SCREEN_BOTTOM = 1

    /** One DS screen, in pixels; AZDS_SCREEN_WIDTH / _HEIGHT in azalon_ds.h. */
    const val SCREEN_WIDTH = 256
    const val SCREEN_HEIGHT = 192

    /** Set while a game is running, so the rest of the app can tell DS from 3DS. */
    @Volatile
    var isActive = false
        private set

    /** Invoked when the emulated console powers off by itself. */
    @Volatile
    var onStopped: (() -> Unit)? = null

    init {
        // Idempotent: NativeLibrary usually got there first. libcitra-android.so
        // pulls in libazalon_ds.so, which holds the DS core itself.
        try {
            System.loadLibrary("citra-android")
            nativeInit()
        } catch (ex: UnsatisfiedLinkError) {
            // Citra's own Log is native too, so it cannot be used here.
            android.util.Log.e("DsNative", "the DS core is unavailable", ex)
        }
    }

    /**
     * Makes sure libcitra-android.so (and with it the DS core) is loaded. Calling
     * this runs the initializer above, which is what actually loads the library.
     */
    fun ensureLoaded() = Unit

    /**
     * Creates the console, loads [romPath] and starts emulating.
     * @return null on success, or a message describing why it failed.
     */
    fun start(romPath: String, savePath: String, settings: DsSettings): String? {
        val error = try {
            nativeStart(
                romPath,
                savePath,
                DsPaths.dataDir,
                settings.consoleType,
                settings.directBoot,
                settings.jit,
                settings.threadedRenderer,
                settings.nickname,
                settings.language,
                settings.favoriteColor,
                settings.dsBios9,
                settings.dsBios7,
                settings.dsFirmware,
                settings.dsiBios9,
                settings.dsiBios7,
                settings.dsiFirmware,
                settings.dsiNand,
                settings.dldiEnable,
                settings.dldiSyncFolder,
                settings.wirelessMode,
                settings.internet
            )
        } catch (ex: UnsatisfiedLinkError) {
            // This build has no DS core (ENABLE_DS was off, or the library is missing).
            "The DS core is not available in this build."
        }
        isActive = error == null
        return error
    }

    fun stopEmulation() {
        isActive = false
        try {
            stop()
        } catch (ex: UnsatisfiedLinkError) {
            // Nothing was ever started.
        }
    }

    @JvmStatic
    private external fun nativeInit()

    @Suppress("LongParameterList")
    @JvmStatic
    private external fun nativeStart(
        romPath: String,
        savePath: String,
        dataDir: String,
        consoleType: Int,
        directBoot: Boolean,
        jit: Boolean,
        threadedRenderer: Boolean,
        nickname: String,
        language: Int,
        favoriteColor: Int,
        dsBios9: String,
        dsBios7: String,
        dsFirmware: String,
        dsiBios9: String,
        dsiBios7: String,
        dsiFirmware: String,
        dsiNand: String,
        dldiEnable: Boolean,
        dldiSyncFolder: String,
        wirelessMode: Int,
        internet: Boolean
    ): String?

    @JvmStatic
    private external fun stop()

    @JvmStatic
    external fun isRunning(): Boolean

    @JvmStatic
    external fun isPaused(): Boolean

    @JvmStatic
    external fun setPaused(paused: Boolean)

    @JvmStatic
    external fun reset()

    @JvmStatic
    external fun setFastForward(enabled: Boolean)

    @JvmStatic
    external fun saveState(path: String): Boolean

    @JvmStatic
    external fun loadState(path: String): Boolean

    /** Hands a screen's [Surface] to the core, or null when it goes away. */
    @JvmStatic
    external fun setSurface(screen: Int, surface: Surface?)

    @JvmStatic
    external fun setKeys(mask: Int)

    @JvmStatic
    external fun setTouch(pressed: Boolean, x: Int, y: Int)

    @JvmStatic
    external fun setLidClosed(closed: Boolean)

    @JvmStatic
    external fun getFps(): Double

    /** Called from the emulation thread. */
    @Keep
    @JvmStatic
    fun onEmulationStopped() {
        Log.info("[DS] the emulated console powered off")
        isActive = false
        onStopped?.invoke()
    }
}
