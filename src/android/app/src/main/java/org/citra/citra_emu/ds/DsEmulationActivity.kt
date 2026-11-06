// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.core.os.BundleCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import org.citra.citra_emu.R
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.GameHelper
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.ThemeUtil

/**
 * Runs a DS/DSi cartridge on the melonDS core.
 *
 * Deliberately separate from [org.citra.citra_emu.activities.EmulationActivity]:
 * the DS core has its own session, its own renderer output and its own settings,
 * exactly as the desktop frontend keeps `src/citra_qt/ds` apart from the 3DS one.
 */
class DsEmulationActivity : AppCompatActivity() {

    private lateinit var root: FrameLayout
    private lateinit var screens: DsScreensView
    private lateinit var overlay: DsInputOverlay

    private var settings = DsSettings()
    private var rom: DsRom.Handle? = null
    private var game: Game? = null
    private var romName: String = "game"

    /** -1 = run [game]; 0/1 = boot the DS/DSi firmware menu without a cartridge. */
    private var firmwareConsole = -1

    /** Keys held through the on-screen overlay and through physical buttons. */
    private var overlayKeys = 0
    private var deviceKeys = 0

    /** Set while the user paused the game, so returning to it does not resume. */
    private var pausedByUser = false
    private var started = false

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeUtil.setTheme(this)
        super.onCreate(savedInstanceState)

        settings = DsSettings.load()
        firmwareConsole = intent.getIntExtra(EXTRA_BOOT_FIRMWARE, -1)
        if (firmwareConsole >= 0) {
            settings.consoleType = firmwareConsole
        } else {
            game = resolveGame()
        }

        buildViews()
        enableFullscreenImmersive()
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() = showMenu()
            }
        )

        DsNative.onStopped = { runOnUiThread { onConsoleStopped() } }

        if (!startEmulation()) {
            finish()
        }
    }

    private fun resolveGame(): Game? {
        intent.extras?.let { extras ->
            runCatching { BundleCompat.getParcelable(extras, EXTRA_GAME, Game::class.java) }
                .getOrNull()
                ?.let { return it }
        }
        return GameHelper.getGameFromIntent(this, intent)
    }

    private fun buildViews() {
        root = FrameLayout(this).apply {
            setBackgroundColor(Color.BLACK)
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }

        screens = DsScreensView(this).apply {
            layoutMode = settings.layout
            swapped = settings.swapScreens
            reserveForControls = settings.showOverlay
        }
        overlay = DsInputOverlay(this).apply {
            visibility = if (settings.showOverlay) View.VISIBLE else View.GONE
        }

        root.addView(
            screens,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )
        root.addView(
            overlay,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        // One listener for the whole window, so a finger on the touch screen and a
        // finger on a button are handled in the same pass.
        root.setOnTouchListener { _, event -> onScreenTouch(event) }
        setContentView(root)
    }

    private fun firmwareTitle(): String? = when (firmwareConsole) {
        0 -> getString(R.string.ds_firmware_title_ds)
        1 -> getString(R.string.ds_firmware_title_dsi)
        else -> null
    }

    private fun startFirmware(): Boolean {
        // Savestates of the system menu get their own slot files.
        romName = if (firmwareConsole == 1) "dsi_firmware" else "ds_firmware"
        val error = DsNative.start("", "", settings)
        if (error != null) {
            showFatal(error)
            return false
        }
        started = true
        title = firmwareTitle()
        Log.info("[DS] running the ${if (firmwareConsole == 1) "DSi" else "DS"} firmware")
        return true
    }

    private fun startEmulation(): Boolean {
        if (firmwareConsole >= 0) return startFirmware()
        val target = game
        if (target == null) {
            showFatal(getString(R.string.ds_error_no_game))
            return false
        }

        val handle = DsRom.open(target.path, target.description)
        if (handle == null) {
            showFatal(getString(R.string.ds_error_open_failed, target.title))
            return false
        }
        rom = handle

        romName = DsRom.baseName(target.path, target.filename)
        val savePath = DsPaths.savePath(romName)
        importSideSave(target, savePath)

        val error = DsNative.start(handle.path, savePath, settings)
        if (error != null) {
            handle.close()
            rom = null
            showFatal(error)
            return false
        }

        started = true
        title = target.title
        Log.info("[DS] running ${target.title}")
        return true
    }

    /**
     * melonDS keeps saves next to the ROM. If one is there and we have no save of
     * our own yet, take it over, so a library carried across from melonDS keeps
     * its progress. Only possible for ROMs we can reach as real files.
     */
    private fun importSideSave(target: Game, savePath: String) {
        if (File(savePath).exists()) return
        val romPath = target.path.removePrefix("!")
        if (!romPath.startsWith("/")) return

        val sideSave = File(File(romPath).parentFile, "$romName.sav")
        if (!sideSave.isFile) return
        runCatching { sideSave.copyTo(File(savePath), overwrite = false) }
            .onSuccess { Log.info("[DS] imported the save next to the ROM") }
            .onFailure { Log.warning("[DS] could not import $sideSave: ${it.message}") }
    }

    // ---------------------------------------------------------------- input

    private fun onScreenTouch(event: MotionEvent): Boolean {
        var keys = 0
        var touching = false
        var touchX = 0
        var touchY = 0

        val released = when (event.actionMasked) {
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> -2 // every pointer is gone
            MotionEvent.ACTION_POINTER_UP -> event.actionIndex
            else -> -1
        }

        if (released != -2) {
            for (i in 0 until event.pointerCount) {
                if (i == released) continue
                val x = event.getX(i).toInt()
                val y = event.getY(i).toInt()

                val overlayHit = if (overlay.isVisible()) overlay.keysAt(x, y) else 0
                if (overlayHit != 0) {
                    keys = keys or overlayHit
                    continue
                }
                // Not on a button: the first such finger drives the touch screen.
                if (!touching) {
                    val point = toTouchScreen(x, y)
                    if (point != null) {
                        touching = true
                        touchX = point.first
                        touchY = point.second
                    }
                }
            }
        }

        overlayKeys = keys
        overlay.setHeldKeys(keys)
        DsNative.setKeys(overlayKeys or deviceKeys)
        DsNative.setTouch(touching, touchX, touchY)
        return true
    }

    /** Maps a window position to DS touch screen coordinates, or null if outside it. */
    private fun toTouchScreen(x: Int, y: Int): Pair<Int, Int>? {
        val view = screens.touchScreenView
        if (view.width <= 0 || view.height <= 0) return null

        val localX = x - (screens.left + view.left)
        val localY = y - (screens.top + view.top)
        if (localX < 0 || localY < 0 || localX >= view.width || localY >= view.height) {
            return null
        }
        val dsX = localX * DsNative.SCREEN_WIDTH / view.width
        val dsY = localY * DsNative.SCREEN_HEIGHT / view.height
        return dsX to dsY
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val key = KEY_MAP[keyCode] ?: return super.onKeyDown(keyCode, event)
        deviceKeys = deviceKeys or key
        DsNative.setKeys(overlayKeys or deviceKeys)
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        val key = KEY_MAP[keyCode] ?: return super.onKeyUp(keyCode, event)
        deviceKeys = deviceKeys and key.inv()
        DsNative.setKeys(overlayKeys or deviceKeys)
        return true
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if ((event.source and InputDevice.SOURCE_CLASS_JOYSTICK) == 0) {
            return super.onGenericMotionEvent(event)
        }
        // The left stick doubles as the D-pad, like on the desktop frontend.
        val x = event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_Y)
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)

        var directions = 0
        if (x > STICK_THRESHOLD || hatX > STICK_THRESHOLD) {
            directions = directions or DsNative.Key.RIGHT
        }
        if (x < -STICK_THRESHOLD || hatX < -STICK_THRESHOLD) {
            directions = directions or DsNative.Key.LEFT
        }
        if (y < -STICK_THRESHOLD || hatY < -STICK_THRESHOLD) {
            directions = directions or DsNative.Key.UP
        }
        if (y > STICK_THRESHOLD || hatY > STICK_THRESHOLD) {
            directions = directions or DsNative.Key.DOWN
        }

        deviceKeys = (deviceKeys and DPAD_KEYS.inv()) or directions
        DsNative.setKeys(overlayKeys or deviceKeys)
        return true
    }

    // ----------------------------------------------------------------- menu

    private fun showMenu() {
        val paused = DsNative.isPaused()
        val items = arrayOf(
            getString(if (paused) R.string.ds_menu_resume else R.string.ds_menu_pause),
            getString(R.string.ds_menu_save_state),
            getString(R.string.ds_menu_load_state),
            getString(R.string.ds_menu_layout),
            getString(R.string.ds_menu_swap_screens),
            getString(R.string.ds_menu_toggle_overlay),
            getString(R.string.ds_menu_close_lid),
            getString(R.string.ds_menu_reset),
            getString(R.string.ds_menu_lan),
            getString(R.string.ds_menu_settings),
            getString(R.string.ds_menu_exit)
        )

        // Pause while the menu is up, unless the game was already paused.
        val resumeAfter = !paused
        if (resumeAfter) DsNative.setPaused(true)

        MaterialAlertDialogBuilder(this)
            .setTitle(game?.title ?: firmwareTitle() ?: getString(R.string.ds_title))
            .setItems(items) { _, which ->
                when (which) {
                    // `paused` is the state from before the menu opened, so flip it.
                    0 -> setPaused(!paused)
                    1 -> saveState(resumeAfter)
                    2 -> loadState(resumeAfter)
                    3 -> showLayoutMenu(resumeAfter)
                    4 -> {
                        screens.swapped = !screens.swapped
                        DsSettings.saveLayout(screens.layoutMode, screens.swapped)
                        if (resumeAfter) DsNative.setPaused(false)
                    }
                    5 -> {
                        val show = !overlay.isVisible()
                        overlay.visibility = if (show) View.VISIBLE else View.GONE
                        screens.reserveForControls = show
                        DsSettings.saveShowOverlay(show)
                        if (resumeAfter) DsNative.setPaused(false)
                    }
                    6 -> closeLid(resumeAfter)
                    7 -> confirmReset(resumeAfter)
                    8 -> {
                        // The lobby refuses to switch wireless modes under a running game.
                        startActivity(Intent(this, DsLanActivity::class.java))
                        if (resumeAfter) DsNative.setPaused(false)
                    }
                    9 -> {
                        startActivity(Intent(this, DsSettingsActivity::class.java))
                        if (resumeAfter) DsNative.setPaused(false)
                    }
                    else -> confirmExit(resumeAfter)
                }
            }
            .setOnCancelListener { if (resumeAfter) DsNative.setPaused(false) }
            .show()
    }

    private fun setPaused(paused: Boolean) {
        pausedByUser = paused
        DsNative.setPaused(paused)
    }

    private fun saveState(resumeAfter: Boolean) {
        pickSlot(R.string.ds_menu_save_state) { slot ->
            val ok = DsNative.saveState(DsPaths.statePath(romName, slot))
            toast(if (ok) R.string.ds_state_saved else R.string.ds_state_failed)
            if (resumeAfter) DsNative.setPaused(false)
        }
    }

    private fun loadState(resumeAfter: Boolean) {
        pickSlot(R.string.ds_menu_load_state) { slot ->
            val path = DsPaths.statePath(romName, slot)
            val ok = File(path).isFile && DsNative.loadState(path)
            toast(if (ok) R.string.ds_state_loaded else R.string.ds_state_failed)
            if (resumeAfter) DsNative.setPaused(false)
        }
    }

    private fun pickSlot(titleId: Int, onPicked: (Int) -> Unit) {
        val slots = Array(STATE_SLOTS) { index ->
            val slot = index + 1
            val file = File(DsPaths.statePath(romName, slot))
            if (file.isFile) {
                getString(R.string.ds_state_slot_used, slot)
            } else {
                getString(R.string.ds_state_slot_empty, slot)
            }
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(titleId)
            .setItems(slots) { _, which -> onPicked(which + 1) }
            .setOnCancelListener { DsNative.setPaused(pausedByUser) }
            .show()
    }

    private fun showLayoutMenu(resumeAfter: Boolean) {
        val options = arrayOf(
            getString(R.string.ds_layout_vertical),
            getString(R.string.ds_layout_horizontal),
            getString(R.string.ds_layout_top_only),
            getString(R.string.ds_layout_bottom_only)
        )
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_menu_layout)
            .setSingleChoiceItems(options, screens.layoutMode.value) { dialog, which ->
                screens.layoutMode = DsScreenLayout.fromInt(which)
                DsSettings.saveLayout(screens.layoutMode, screens.swapped)
                dialog.dismiss()
                if (resumeAfter) DsNative.setPaused(false)
            }
            .setOnCancelListener { if (resumeAfter) DsNative.setPaused(false) }
            .show()
    }

    /** Closes the lid for a moment, which is what games use it for. */
    private fun closeLid(resumeAfter: Boolean) {
        DsNative.setLidClosed(true)
        root.postDelayed({ DsNative.setLidClosed(false) }, LID_CLOSED_MS)
        if (resumeAfter) DsNative.setPaused(false)
    }

    private fun confirmReset(resumeAfter: Boolean) {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_menu_reset)
            .setMessage(R.string.ds_reset_confirm)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                DsNative.reset()
                if (resumeAfter) DsNative.setPaused(false)
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                if (resumeAfter) DsNative.setPaused(false)
            }
            .setOnCancelListener { if (resumeAfter) DsNative.setPaused(false) }
            .show()
    }

    private fun confirmExit(resumeAfter: Boolean) {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_menu_exit)
            .setMessage(R.string.ds_exit_confirm)
            .setPositiveButton(android.R.string.ok) { _, _ -> finish() }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                if (resumeAfter) DsNative.setPaused(false)
            }
            .setOnCancelListener { if (resumeAfter) DsNative.setPaused(false) }
            .show()
    }

    // ------------------------------------------------------------ lifecycle

    override fun onResume() {
        super.onResume()
        if (started && !pausedByUser) {
            DsNative.setPaused(false)
        }
    }

    override fun onPause() {
        // Keep emulating in the background? No: the DS core would run unseen and
        // keep the audio device. Pausing also flushes nothing, so saves are safe.
        if (started) {
            DsNative.setPaused(true)
        }
        super.onPause()
    }

    override fun onDestroy() {
        DsNative.onStopped = null
        if (started) {
            DsNative.stopEmulation() // flushes the cartridge save
            started = false
        }
        rom?.close()
        rom = null
        super.onDestroy()
    }

    private fun onConsoleStopped() {
        if (isFinishing) return
        toast(R.string.ds_console_powered_off)
        finish()
    }

    // ----------------------------------------------------------------- misc

    private fun enableFullscreenImmersive() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    private fun showFatal(message: String) {
        Log.error("[DS] $message")
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    private fun toast(resourceId: Int) =
        Toast.makeText(this, resourceId, Toast.LENGTH_SHORT).show()

    private fun View.isVisible() = visibility == View.VISIBLE

    companion object {
        const val EXTRA_GAME = "game"

        /** Int extra: 0 = DS, 1 = DSi. Boots that console's menu instead of a game. */
        const val EXTRA_BOOT_FIRMWARE = "boot_firmware"

        /**
         * Boots the DS or DSi system menu. Real dumps are required: FreeBIOS has no
         * menu, so without them this explains what to set instead of showing black.
         */
        fun launchFirmware(activity: android.app.Activity, dsi: Boolean) {
            val s = DsSettings.load()
            val missing = if (dsi) {
                listOf(s.dsiBios9, s.dsiBios7, s.dsiFirmware, s.dsiNand).any { !File(it).isFile }
            } else {
                listOf(s.dsBios9, s.dsBios7, s.dsFirmware).any { !File(it).isFile }
            }
            if (missing) {
                MaterialAlertDialogBuilder(activity)
                    .setTitle(R.string.ds_home_boot_firmware)
                    .setMessage(
                        if (dsi) R.string.ds_firmware_missing_dsi else R.string.ds_firmware_missing_ds
                    )
                    .setPositiveButton(R.string.ds_firmware_open_settings) { _, _ ->
                        DsSettingsActivity.launch(activity)
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
                return
            }
            activity.startActivity(
                Intent(activity, DsEmulationActivity::class.java)
                    .putExtra(EXTRA_BOOT_FIRMWARE, if (dsi) 1 else 0)
            )
        }

        /** Asks DS or DSi, then [launchFirmware]. */
        fun pickAndLaunchFirmware(activity: android.app.Activity) {
            val choices = arrayOf(
                activity.getString(R.string.ds_firmware_ds),
                activity.getString(R.string.ds_firmware_dsi)
            )
            MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.ds_firmware_pick)
                .setItems(choices) { _, which -> launchFirmware(activity, which == 1) }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        private const val STATE_SLOTS = 8
        private const val LID_CLOSED_MS = 600L
        private const val STICK_THRESHOLD = 0.5f

        private const val DPAD_KEYS =
            DsNative.Key.UP or DsNative.Key.DOWN or DsNative.Key.LEFT or DsNative.Key.RIGHT

        /** Physical controller and keyboard bindings. */
        private val KEY_MAP = mapOf(
            KeyEvent.KEYCODE_BUTTON_A to DsNative.Key.B,
            KeyEvent.KEYCODE_BUTTON_B to DsNative.Key.A,
            KeyEvent.KEYCODE_BUTTON_X to DsNative.Key.Y,
            KeyEvent.KEYCODE_BUTTON_Y to DsNative.Key.X,
            KeyEvent.KEYCODE_BUTTON_L1 to DsNative.Key.L,
            KeyEvent.KEYCODE_BUTTON_R1 to DsNative.Key.R,
            KeyEvent.KEYCODE_BUTTON_START to DsNative.Key.START,
            KeyEvent.KEYCODE_BUTTON_SELECT to DsNative.Key.SELECT,
            KeyEvent.KEYCODE_DPAD_UP to DsNative.Key.UP,
            KeyEvent.KEYCODE_DPAD_DOWN to DsNative.Key.DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT to DsNative.Key.LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT to DsNative.Key.RIGHT,
            // Keyboard, for TV boxes and desktops running the Android build.
            KeyEvent.KEYCODE_Z to DsNative.Key.B,
            KeyEvent.KEYCODE_X to DsNative.Key.A,
            KeyEvent.KEYCODE_A to DsNative.Key.Y,
            KeyEvent.KEYCODE_S to DsNative.Key.X,
            KeyEvent.KEYCODE_Q to DsNative.Key.L,
            KeyEvent.KEYCODE_W to DsNative.Key.R,
            KeyEvent.KEYCODE_ENTER to DsNative.Key.START,
            KeyEvent.KEYCODE_SPACE to DsNative.Key.SELECT
        )
    }
}
