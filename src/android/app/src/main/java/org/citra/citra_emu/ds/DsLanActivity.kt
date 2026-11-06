// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputFilter
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.preference.PreferenceManager
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.net.Inet4Address
import java.net.NetworkInterface
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.ThemeUtil

/**
 * DS local wireless lobby: find rooms on the network, host one, or join by IP.
 * The Android counterpart of the desktop's `DS::LanDialog`.
 *
 * Host or join here, then start the same game on every device: the game's own
 * "DS Wireless Play"/"Download Play" menu then sees the other players.
 */
class DsLanActivity : AppCompatActivity() {

    private val handler = Handler(Looper.getMainLooper())
    private var multicastLock: WifiManager.MulticastLock? = null

    private lateinit var nameInput: EditText
    private lateinit var browseSection: LinearLayout
    private lateinit var roomList: LinearLayout
    private lateinit var roomsEmpty: TextView
    private lateinit var hostSection: LinearLayout
    private lateinit var roomNameInput: EditText
    private lateinit var passwordInput: EditText
    private lateinit var maxPlayersInput: EditText
    private lateinit var sessionSection: LinearLayout
    private lateinit var statusText: TextView
    private lateinit var playerList: LinearLayout
    private lateinit var chatLog: TextView
    private lateinit var chatInput: EditText

    private var lastRooms: List<DsLan.Room> = emptyList()
    private var lastPlayers: List<DsLan.Player> = emptyList()
    private var lastChat = ""
    private var shownInSession: Boolean? = null

    private val refreshTask = object : Runnable {
        override fun run() {
            refresh()
            handler.postDelayed(this, REFRESH_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeUtil.setTheme(this)
        super.onCreate(savedInstanceState)
        title = getString(R.string.ds_lan_title)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        if (!DsLan.useLan()) {
            Toast.makeText(this, R.string.ds_lan_stop_game_first, Toast.LENGTH_LONG).show()
            finish()
            return
        }
        buildViews()
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }

    override fun onResume() {
        super.onResume()
        acquireMulticastLock()
        if (!DsLan.inSession && !DsLan.busy) {
            DsLan.startDiscovery()
        }
        DsLan.startPump()
        handler.post(refreshTask)
    }

    override fun onPause() {
        handler.removeCallbacks(refreshTask)
        if (!DsLan.inSession && !DsLan.busy) {
            // Only browsing: stop listening. A live session keeps the pump running,
            // so the room survives until the game is started.
            DsLan.endDiscovery()
            DsLan.stopPump()
        }
        releaseMulticastLock()
        super.onPause()
    }

    override fun onDestroy() {
        if (::nameInput.isInitialized) {
            PreferenceManager.getDefaultSharedPreferences(this).edit()
                .putString(DsLan.KEY_PLAYER_NAME, nameInput.text.toString().trim())
                .apply()
        }
        super.onDestroy()
    }

    /** Many phones drop broadcast UDP (room discovery) unless an app asks for it. */
    private fun acquireMulticastLock() {
        if (multicastLock?.isHeld == true) return
        val wifi = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        multicastLock = runCatching {
            wifi?.createMulticastLock("azalon-ds-lan")?.apply {
                setReferenceCounted(false)
                acquire()
            }
        }.getOrNull()
    }

    private fun releaseMulticastLock() {
        runCatching { multicastLock?.takeIf { it.isHeld }?.release() }
        multicastLock = null
    }

    // ---------------------------------------------------------------- views

    private fun dp(value: Int): Int = TypedValue.applyDimension(
        TypedValue.COMPLEX_UNIT_DIP,
        value.toFloat(),
        resources.displayMetrics
    ).toInt()

    private fun buildViews() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(this)
        val defaultName = prefs.getString(DsLan.KEY_PLAYER_NAME, null)
            ?.takeIf { it.isNotBlank() }
            ?: DsSettings.load().nickname

        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(8), dp(16), dp(24))
        }
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            addView(column)
        }
        ViewCompat.setOnApplyWindowInsetsListener(scroll) { view, insets ->
            val bars = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
            )
            view.updatePadding(left = bars.left, right = bars.right, bottom = bars.bottom)
            insets
        }

        nameInput = textInput(R.string.ds_lan_your_name, defaultName, 31)
        column.addView(nameInput)

        column.addView(note(getString(R.string.ds_lan_this_device, localAddresses())))

        // Rooms on the network
        browseSection = section(column, R.string.ds_lan_rooms)
        roomsEmpty = note(getString(R.string.ds_lan_searching))
        browseSection.addView(roomsEmpty)
        roomList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        browseSection.addView(roomList)
        browseSection.addView(button(R.string.ds_lan_join_by_ip, outlined = true) { askAddress() })

        // Hosting
        hostSection = section(column, R.string.ds_lan_host_room)
        roomNameInput = textInput(R.string.ds_lan_room_name, getString(R.string.ds_lan_default_room, defaultName), 63)
        passwordInput = textInput(R.string.ds_lan_password_optional, "", 32).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        maxPlayersInput = textInput(R.string.ds_lan_max_players, "4", 2).apply {
            inputType = InputType.TYPE_CLASS_NUMBER
        }
        hostSection.addView(roomNameInput)
        hostSection.addView(passwordInput)
        hostSection.addView(maxPlayersInput)
        hostSection.addView(button(R.string.ds_lan_host) { host() })

        // Current session
        sessionSection = section(column, R.string.ds_lan_session)
        statusText = TextView(this).apply {
            setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodyLarge)
        }
        sessionSection.addView(statusText)
        sessionSection.addView(note(getString(R.string.ds_lan_session_hint)))
        playerList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        sessionSection.addView(playerList)

        chatLog = TextView(this).apply {
            setTextIsSelectable(true)
            setPadding(0, dp(8), 0, dp(8))
        }
        sessionSection.addView(chatLog)
        val chatRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        chatInput = EditText(this).apply {
            hint = getString(R.string.ds_lan_chat_hint)
            isSingleLine = true
            imeOptions = EditorInfo.IME_ACTION_SEND
            filters = arrayOf(InputFilter.LengthFilter(255))
            setOnEditorActionListener { _, action, _ ->
                if (action == EditorInfo.IME_ACTION_SEND) {
                    sendChat()
                    true
                } else {
                    false
                }
            }
        }
        chatRow.addView(chatInput, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        chatRow.addView(button(R.string.ds_lan_send, outlined = true) { sendChat() })
        sessionSection.addView(chatRow)
        sessionSection.addView(button(R.string.ds_lan_leave, outlined = true) { leave() })

        setContentView(scroll)
        updateSessionState()
    }

    private fun section(parent: LinearLayout, titleId: Int): LinearLayout {
        val inner = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(12))
        }
        inner.addView(
            TextView(this).apply {
                text = getString(titleId)
                setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_TitleMedium)
                setPadding(0, 0, 0, dp(4))
            }
        )
        val card = MaterialCardView(this).apply { addView(inner) }
        parent.addView(
            card,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(12) }
        )
        return inner
    }

    private fun note(text: String) = TextView(this).apply {
        this.text = text
        setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodySmall)
        setPadding(0, dp(2), 0, dp(6))
    }

    private fun textInput(hintId: Int, value: String, maxLength: Int) = EditText(this).apply {
        hint = getString(hintId)
        setText(value)
        isSingleLine = true
        filters = arrayOf(InputFilter.LengthFilter(maxLength))
    }

    private fun button(textId: Int, outlined: Boolean = false, onClick: () -> Unit) =
        MaterialButton(
            this,
            null,
            if (outlined) {
                com.google.android.material.R.attr.materialButtonOutlinedStyle
            } else {
                com.google.android.material.R.attr.materialButtonStyle
            }
        ).apply {
            text = getString(textId)
            setOnClickListener { onClick() }
        }

    // -------------------------------------------------------------- actions

    private fun playerName(): String? {
        val name = nameInput.text.toString().trim()
        if (name.isEmpty()) {
            Toast.makeText(this, R.string.ds_lan_enter_name, Toast.LENGTH_SHORT).show()
            nameInput.requestFocus()
            return null
        }
        return name
    }

    private fun host() {
        val name = playerName() ?: return
        val maxPlayers = (maxPlayersInput.text.toString().toIntOrNull() ?: 4).coerceIn(2, 16)
        val room = roomNameInput.text.toString().trim().ifEmpty {
            getString(R.string.ds_lan_default_room, name)
        }
        DsLan.endDiscovery()
        if (!DsLan.host(name, maxPlayers, room, passwordInput.text.toString())) {
            DsLan.startDiscovery()
            showError(getString(R.string.ds_lan_host_failed))
            return
        }
        DsLan.startPump()
        refresh()
    }

    private fun askAddress() {
        val input = EditText(this).apply {
            hint = "192.168.1.20"
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        val box = LinearLayout(this).apply {
            setPadding(dp(24), dp(8), dp(24), 0)
            addView(input, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_lan_join_by_ip)
            .setMessage(R.string.ds_lan_host_address)
            .setView(box)
            .setPositiveButton(R.string.ds_lan_join) { _, _ ->
                val address = input.text.toString().trim()
                if (address.isNotEmpty()) askPasswordThenJoin(address, maybeHasPassword = true)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /**
     * Rooms found by discovery say whether they need a password. A typed address
     * may or may not: ask, and an empty answer joins without one.
     */
    private fun askPasswordThenJoin(address: String, maybeHasPassword: Boolean, required: Boolean = false) {
        if (!maybeHasPassword) {
            join(address, "")
            return
        }
        val input = EditText(this).apply {
            hint = getString(if (required) R.string.ds_lan_password else R.string.ds_lan_password_optional)
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val box = LinearLayout(this).apply {
            setPadding(dp(24), dp(8), dp(24), 0)
            addView(input, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_lan_password)
            .setView(box)
            .setPositiveButton(R.string.ds_lan_join) { _, _ -> join(address, input.text.toString()) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun join(address: String, password: String) {
        val name = playerName() ?: return
        if (DsLan.busy) return

        DsLan.endDiscovery()
        DsLan.busy = true
        val progress = MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_lan_joining)
            .setMessage(address)
            .setCancelable(false)
            .show()

        // Connecting blocks until the host answers (up to ~10 s).
        Thread({
            val ok = runCatching { DsLan.join(name, address, password) }.getOrDefault(false)
            handler.post {
                DsLan.busy = false
                if (!isDestroyed) progress.dismiss()
                if (ok) {
                    DsLan.startPump()
                } else {
                    DsLan.startDiscovery()
                    if (!isDestroyed) showError(getString(R.string.ds_lan_join_failed, address))
                }
                if (!isDestroyed) refresh()
            }
        }, "DsLanJoin").start()
    }

    private fun leave() {
        DsLan.leave()
        lastChat = ""
        chatLog.text = ""
        DsLan.startDiscovery()
        refresh()
    }

    private fun sendChat() {
        val text = chatInput.text.toString().trim()
        if (text.isEmpty() || !DsLan.inSession) return
        DsLan.sendChat(text)
        chatInput.setText("")
        refresh()
    }

    private fun showError(message: String) {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ds_lan_title)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    // -------------------------------------------------------------- refresh

    private fun updateSessionState() {
        val inSession = DsLan.inSession
        if (shownInSession == inSession) return
        shownInSession = inSession
        browseSection.parentView().visibility = if (inSession) View.GONE else View.VISIBLE
        hostSection.parentView().visibility = if (inSession) View.GONE else View.VISIBLE
        sessionSection.parentView().visibility = if (inSession) View.VISIBLE else View.GONE
        nameInput.isEnabled = !inSession
    }

    private fun View.parentView(): View = parent as View

    private fun refresh() {
        if (DsLan.busy) return
        updateSessionState()

        if (!DsLan.inSession) {
            val rooms = DsLan.rooms()
            if (rooms != lastRooms) {
                lastRooms = rooms
                roomList.removeAllViews()
                roomsEmpty.text = getString(
                    if (rooms.isEmpty()) R.string.ds_lan_searching else R.string.ds_lan_tap_to_join
                )
                for (room in rooms) roomList.addView(roomRow(room))
            }
            return
        }

        val players = DsLan.players()
        statusText.text = getString(
            R.string.ds_lan_connected,
            DsLan.numPlayers(),
            DsLan.maxPlayers()
        )
        if (players != lastPlayers) {
            lastPlayers = players
            playerList.removeAllViews()
            val amHost = players.any { it.isLocal && it.status == DsLan.STATUS_HOST }
            for (player in players) playerList.addView(playerRow(player, amHost))
        }

        val chat = DsLan.getChat()?.joinToString("\n").orEmpty()
        if (chat != lastChat) {
            lastChat = chat
            chatLog.text = chat
        }
    }

    private fun roomRow(room: DsLan.Room): View {
        val lock = if (room.hasPassword) " 🔒" else ""
        val label = buildString {
            append(room.name.ifEmpty { room.address })
            append(lock)
            append("\n")
            append(getString(R.string.ds_lan_room_details, room.players, room.maxPlayers, room.address))
            if (room.inGame) append(" · ").append(getString(R.string.ds_lan_in_game))
        }
        return MaterialButton(
            this,
            null,
            com.google.android.material.R.attr.materialButtonOutlinedStyle
        ).apply {
            text = label
            isAllCaps = false
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setOnClickListener {
                askPasswordThenJoin(room.address, room.hasPassword, required = room.hasPassword)
            }
        }
    }

    private fun playerRow(player: DsLan.Player, amHost: Boolean): View {
        val status = when (player.status) {
            DsLan.STATUS_HOST -> getString(R.string.ds_lan_status_host)
            DsLan.STATUS_CLIENT -> getString(R.string.ds_lan_status_client)
            DsLan.STATUS_CONNECTING -> getString(R.string.ds_lan_status_connecting)
            DsLan.STATUS_DISCONNECTED -> getString(R.string.ds_lan_status_disconnected)
            else -> ""
        }
        val you = if (player.isLocal) getString(R.string.ds_lan_you) else ""
        val ping = if (player.isLocal) "" else " · ${player.ping} ms"
        return TextView(this).apply {
            text = "${player.id + 1}. ${player.name}$you · $status · ${player.address}$ping"
            setPadding(0, dp(4), 0, dp(4))
            if (amHost && !player.isLocal) {
                setOnLongClickListener {
                    MaterialAlertDialogBuilder(this@DsLanActivity)
                        .setMessage(getString(R.string.ds_lan_kick_confirm, player.name))
                        .setPositiveButton(R.string.ds_lan_kick) { _, _ -> DsLan.kick(player.id) }
                        .setNegativeButton(android.R.string.cancel, null)
                        .show()
                    true
                }
            }
        }
    }

    /** IPv4 addresses of this device, so the other side can "Join by IP". */
    private fun localAddresses(): String = runCatching {
        NetworkInterface.getNetworkInterfaces().toList()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { it.inetAddresses.toList() }
            .filterIsInstance<Inet4Address>()
            .joinToString(", ") { it.hostAddress.orEmpty() }
    }.getOrNull()?.ifEmpty { null } ?: "?"

    companion object {
        private const val REFRESH_MS = 250L
    }
}
