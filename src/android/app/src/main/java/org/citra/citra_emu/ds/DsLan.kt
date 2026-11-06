// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.os.Handler
import android.os.Looper
import androidx.annotation.Keep
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication

/**
 * The melonDS LAN lobby (src/android/app/src/main/jni/ds/ds_lan_jni.cpp).
 *
 * Same protocol and calls as the desktop's `DS::LanDialog`, so phones and PCs on
 * one network can host and join each other's rooms (UDP 7063 for discovery,
 * UDP 7064 for the session).
 *
 * Everything here runs on the main thread, except [join], which blocks while the
 * host answers. While no game runs, nobody else drives the network, so [pump]
 * keeps calling [process]; once a game runs, its emulation thread does that.
 */
@Keep
object DsLan {
    data class Room(
        val address: String,
        val code: String,
        val name: String,
        val game: String,
        val description: String,
        val players: Int,
        val maxPlayers: Int,
        val hasPassword: Boolean,
        val inGame: Boolean
    )

    data class Player(
        val id: Int,
        val name: String,
        val status: Int,
        val address: String,
        val isLocal: Boolean,
        val ping: Int
    )

    // LAN::PlayerStatus
    const val STATUS_CLIENT = 1
    const val STATUS_HOST = 2
    const val STATUS_CONNECTING = 3
    const val STATUS_DISCONNECTED = 4

    const val KEY_PLAYER_NAME = "ds_lan_player_name"

    private const val ROOM_FIELDS = 9
    private const val PLAYER_FIELDS = 6
    private const val PUMP_INTERVAL_MS = 50L

    private val mainHandler = Handler(Looper.getMainLooper())

    /** Set while [join] runs on a worker thread; nothing else may touch the LAN then. */
    @Volatile
    var busy = false
        internal set

    private var pumping = false
    private val pumpTask = object : Runnable {
        override fun run() {
            if (!pumping) return
            if (!busy && !gameRunning()) {
                process()
            }
            mainHandler.postDelayed(this, PUMP_INTERVAL_MS)
        }
    }

    init {
        DsNative.ensureLoaded()
    }

    private fun gameRunning(): Boolean = DsNative.isActive || runCatching { DsNative.isRunning() }
        .getOrDefault(false)

    /**
     * Switches the core to LAN wireless and remembers it, so games started from
     * now on keep the session. Returns false while a game runs on another mode.
     */
    fun useLan(): Boolean {
        val ok = try {
            nativeUseLan(gameRunning())
        } catch (ex: UnsatisfiedLinkError) {
            false
        }
        if (ok) {
            PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
                .edit()
                .putString(DsSettings.KEY_WIRELESS_MODE, "1")
                .apply()
        }
        return ok
    }

    /** Keeps the network serviced until [stopPump]. Safe to call repeatedly. */
    fun startPump() {
        if (pumping) return
        pumping = true
        mainHandler.post(pumpTask)
    }

    fun stopPump() {
        pumping = false
        mainHandler.removeCallbacks(pumpTask)
    }

    val inSession: Boolean
        get() = !busy && runCatching { nativeInSession() }.getOrDefault(false)

    fun rooms(): List<Room> {
        val flat = nativeGetRooms() ?: return emptyList()
        return (0 until flat.size / ROOM_FIELDS).map { i ->
            val f = i * ROOM_FIELDS
            Room(
                address = flat[f],
                code = flat[f + 1],
                name = flat[f + 2],
                game = flat[f + 3],
                description = flat[f + 4],
                players = flat[f + 5].toIntOrNull() ?: 0,
                maxPlayers = flat[f + 6].toIntOrNull() ?: 0,
                hasPassword = flat[f + 7] == "1",
                inGame = flat[f + 8] == "1"
            )
        }
    }

    fun players(): List<Player> {
        val flat = nativeGetPlayers() ?: return emptyList()
        return (0 until flat.size / PLAYER_FIELDS).map { i ->
            val f = i * PLAYER_FIELDS
            Player(
                id = flat[f].toIntOrNull() ?: 0,
                name = flat[f + 1],
                status = flat[f + 2].toIntOrNull() ?: 0,
                address = flat[f + 3],
                isLocal = flat[f + 4] == "1",
                ping = flat[f + 5].toIntOrNull() ?: 0
            )
        }
    }

    private external fun nativeUseLan(gameRunning: Boolean): Boolean

    external fun isLanMode(): Boolean

    /** Services discovery and the session once. Main thread only. */
    external fun process()

    external fun startDiscovery(): Boolean

    external fun endDiscovery()

    private external fun nativeGetRooms(): Array<String>?

    external fun host(playerName: String, maxPlayers: Int, roomName: String, password: String): Boolean

    /** Blocks for up to ~10 seconds: call from a worker thread with [busy] set. */
    external fun join(playerName: String, address: String, password: String): Boolean

    external fun leave()

    private external fun nativeGetPlayers(): Array<String>?

    private external fun nativeInSession(): Boolean

    external fun numPlayers(): Int

    external fun maxPlayers(): Int

    external fun sendChat(message: String)

    external fun getChat(): Array<String>?

    external fun kick(playerId: Int): Boolean
}
