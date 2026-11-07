// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
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

    data class Chat(val senderId: Int, val sender: String, val isLocal: Boolean, val text: String)

    // LAN::PlayerStatus
    const val STATUS_CLIENT = 1
    const val STATUS_HOST = 2
    const val STATUS_CONNECTING = 3
    const val STATUS_DISCONNECTED = 4


    private const val ROOM_FIELDS = 9
    private const val PLAYER_FIELDS = 6
    private const val PUMP_INTERVAL_MS = 50L

    private val mainHandler = Handler(Looper.getMainLooper())

    /** Set while [join] runs on a worker thread; nothing else may touch the LAN then. */
    @Volatile
    var busy = false
        internal set

    /** Set while a room browser is open (discovery needs servicing too). */
    @Volatile
    var browsing = false

    private var pumping = false
    private val pumpTask = object : Runnable {
        override fun run() {
            if (!pumping) return
            if (!busy && !browsing && !inSession) {
                pumping = false // nothing left to service
                return
            }
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

    private var wifiLock: WifiManager.WifiLock? = null

    /**
     * DS wireless is lock-step: every frame the host sends a packet and waits a few
     * milliseconds for each client's reply. With Wi-Fi power saving on (the default
     * on phones) the radio sleeps between access-point beacons and incoming packets
     * arrive up to ~100-300 ms late, so the game stalls on every exchange. Azahar's
     * 3DS multiplayer tolerates that latency; DS wireless does not. A low-latency
     * lock turns power saving off while a DS game runs in a LAN room.
     */
    fun acquireLowLatencyWifi() {
        if (wifiLock?.isHeld == true) return
        val wifi = CitraApplication.appContext.applicationContext
            .getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return
        @Suppress("DEPRECATION")
        val mode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            WifiManager.WIFI_MODE_FULL_LOW_LATENCY
        } else {
            WifiManager.WIFI_MODE_FULL_HIGH_PERF
        }
        wifiLock = runCatching {
            wifi.createWifiLock(mode, "azalon-ds-lan").apply {
                setReferenceCounted(false)
                acquire()
            }
        }.getOrNull()
    }

    fun releaseLowLatencyWifi() {
        runCatching { wifiLock?.takeIf { it.isHeld }?.release() }
        wifiLock = null
    }

    /** Keeps the network serviced while browsing or in a room; stops on its own after. */
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

    /** The room this device hosts or joined, or null. [Room.description] is the host's name. */
    fun currentRoom(): Room? {
        val f = runCatching { nativeCurrentRoom() }.getOrNull() ?: return null
        if (f.size < ROOM_FIELDS) return null
        return Room(
            address = f[0], code = f[1], name = f[2], game = f[3], description = f[4],
            players = f[5].toIntOrNull() ?: 0, maxPlayers = f[6].toIntOrNull() ?: 0,
            hasPassword = f[7] == "1", inGame = f[8] == "1"
        )
    }

    fun chatMessages(): List<Chat> {
        val flat = runCatching { nativeGetChatMessages() }.getOrNull() ?: return emptyList()
        return (0 until flat.size / 4).map { i ->
            Chat(flat[i * 4].toIntOrNull() ?: 0, flat[i * 4 + 1], flat[i * 4 + 2] == "1", flat[i * 4 + 3])
        }
    }

    /** "Not started yet" for rooms whose host has no game running yet. */
    fun gameText(room: Room, notStarted: String, playing: (String) -> String): String =
        if (room.game.isEmpty() || room.game == "Nintendo DS") notStarted
        else if (room.inGame) playing(room.game) else room.game

    private var currentGameTitle = ""
    private var currentGameRunning = false

    /** DsEmulationActivity reports its game; a hosted room advertises it. */
    fun setCurrentGame(title: String, running: Boolean) {
        currentGameTitle = title
        currentGameRunning = running
        if (runCatching { isHost() }.getOrDefault(false)) setGame(title, running)
    }

    /** Call right after hosting, so the room shows a game that is already running. */
    fun advertiseCurrentGame() {
        if (currentGameTitle.isNotEmpty() && runCatching { isHost() }.getOrDefault(false)) {
            setGame(currentGameTitle, currentGameRunning)
        }
    }

    /**
     * Turns polls into "X joined", "X left" and chat events, for screens that only
     * show notifications (the game screen). Starts silent: what is there is not news.
     */
    class Watcher {
        private var known: Map<Int, String>? = null
        private var chatSeen = -1
        private var wasInSession = false

        sealed class Event {
            data class Joined(val name: String) : Event()
            data class Left(val name: String) : Event()
            data class Message(val chat: Chat) : Event()
            object RoomClosed : Event()
        }

        fun poll(): List<Event> {
            val events = mutableListOf<Event>()
            val inRoom = inSession
            if (!inRoom) {
                if (wasInSession) events.add(Event.RoomClosed)
                wasInSession = false
                known = null
                chatSeen = -1
                return events
            }
            wasInSession = true
            val now = players()
                .filter { it.status == STATUS_HOST || it.status == STATUS_CLIENT }
                .associate { it.id to it.name }
            known?.let { before ->
                now.forEach { (id, name) -> if (id !in before) events.add(Event.Joined(name)) }
                before.forEach { (id, name) -> if (id !in now) events.add(Event.Left(name)) }
            }
            known = now
            val total = chatTotal()
            if (chatSeen in 0 until total) {
                val fresh = chatMessages().takeLast(total - chatSeen)
                fresh.filter { !it.isLocal }.forEach { events.add(Event.Message(it)) }
            }
            chatSeen = total
            return events
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

    private external fun nativeGetChatMessages(): Array<String>?

    /** Messages this session, growing past the 100 the core keeps. */
    external fun chatTotal(): Int

    external fun isHost(): Boolean

    private external fun nativeCurrentRoom(): Array<String>?

    private external fun setGame(title: String, inGame: Boolean)

    /** The IPv4 this device uses on its network, or null without one. */
    external fun localAddress(): String?

    external fun kick(playerId: Int): Boolean
}
