// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.res.Configuration
import android.graphics.Color
import android.net.wifi.WifiManager
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.PopupMenu
import android.widget.Toast
import androidx.core.widget.doOnTextChanged
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textfield.TextInputEditText
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogChatBinding
import org.citra.citra_emu.databinding.DialogLobbyBrowserBinding
import org.citra.citra_emu.databinding.DialogMultiplayerLobbyBinding
import org.citra.citra_emu.databinding.DialogMultiplayerRoomBinding
import org.citra.citra_emu.databinding.ItemButtonNetplayBinding
import org.citra.citra_emu.databinding.ItemChatMessageBinding
import org.citra.citra_emu.databinding.ItemLobbyRoomBinding
import org.citra.citra_emu.databinding.ItemTextNetplayBinding
import org.citra.citra_emu.utils.NetPlayManager

/**
 * DS local wireless screens inside Azahar's Multiplayer dialog, built from the same
 * layouts as Azahar's own rooms (lobby browser, create/join sheets, room sheet, chat).
 *
 * The rooms are the DS core's melonDS LAN rooms, the same ones the desktop build
 * hosts and joins, so phones and PCs on one network play together.
 */
object DsNetPlay {
    private const val REFRESH_MS = 500L
    private const val KEY_LAST_ADDRESS = "ds_lan_last_address"

    // Same palette as Azahar's desktop chat room.
    private val PLAYER_COLORS = intArrayOf(
        0xFF3F7FFF.toInt(), 0xFFFF4040.toInt(), 0xFF8A2BE2.toInt(), 0xFFFF69B4.toInt(),
        0xFF1E90FF.toInt(), 0xFF2E9E2E.toInt(), 0xFF00C070.toInt(), 0xFFB22222.toInt(),
        0xFFDAA520.toInt(), 0xFFFF4500.toInt(), 0xFF2E8B57.toInt(), 0xFF5F9EA0.toInt(),
        0xFFD2691E.toInt(), 0xFF9ACD32.toInt(), 0xFFFF7F50.toInt(), 0xFFC0A000.toInt()
    )

    private val handler = Handler(Looper.getMainLooper())
    private var multicastLock: WifiManager.MulticastLock? = null

    /** Lines shown in the room's chat: messages plus join/leave notes. */
    private val chatLog = mutableListOf<ChatLine>()
    private var chatSeen = 0
    private var lastPlayers: Map<Int, String>? = null

    data class ChatLine(
        val sender: String,
        val text: String,
        val color: Int,
        val system: Boolean,
        val time: String = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date())
    )

    // ------------------------------------------------------------------ helpers

    private fun BottomSheetDialog.expand(context: Context) {
        behavior.state = BottomSheetBehavior.STATE_EXPANDED
        behavior.skipCollapsed =
            context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    }

    fun nickname(context: Context): String = NetPlayManager.getUsername(context)

    private fun nicknameValid(name: String): Boolean =
        name.matches(Regex("^[a-zA-Z0-9._\\- ]{4,20}$"))

    fun localAddress(): String? = runCatching { DsLan.localAddress() }.getOrNull()

    /** Many phones drop broadcast UDP (room discovery) unless an app asks for it. */
    private fun acquireMulticast(context: Context) {
        if (multicastLock?.isHeld == true) return
        val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        multicastLock = runCatching {
            wifi?.createMulticastLock("azalon-ds-lan")?.apply {
                setReferenceCounted(false)
                acquire()
            }
        }.getOrNull()
    }

    private fun releaseMulticast() {
        runCatching { multicastLock?.takeIf { it.isHeld }?.release() }
        multicastLock = null
    }

    private fun toast(text: String) =
        Toast.makeText(CitraApplication.appContext, text, Toast.LENGTH_LONG).show()

    private fun prepareLan(context: Context): Boolean {
        if (!DsLan.useLan()) {
            toast(context.getString(R.string.ds_lan_stop_game_first))
            return false
        }
        return true
    }

    private fun gameText(context: Context, room: DsLan.Room): String =
        DsLan.gameText(room, context.getString(R.string.ds_mp_not_started)) {
            context.getString(R.string.ds_mp_playing, it)
        }

    // ------------------------------------------------------------------ entering a room

    private fun resetRoomLog(context: Context, hosted: Boolean) {
        chatLog.clear()
        chatSeen = 0
        lastPlayers = null
        chatLog.add(
            ChatLine(
                "", context.getString(if (hosted) R.string.ds_mp_room_created else R.string.ds_mp_joined_room),
                Color.GRAY, true
            )
        )
    }

    private fun enteredRoom(context: Context, hosted: Boolean) {
        resetRoomLog(context, hosted)
        DsLan.startPump()
        if (hosted) DsLan.advertiseCurrentGame()
        showRoom(context)
    }

    private fun join(context: Context, address: String, password: String, onFail: () -> Unit) {
        if (DsLan.busy || !prepareLan(context)) return
        val name = nickname(context)
        DsLan.endDiscovery()
        DsLan.busy = true
        val progress = MaterialAlertDialogBuilder(context)
            .setTitle(R.string.ds_lan_joining)
            .setMessage(address)
            .setCancelable(false)
            .show()
        // Connecting blocks until the host answers (up to ~10 s).
        Thread({
            val ok = runCatching { DsLan.join(name, address, password) }.getOrDefault(false)
            handler.post {
                DsLan.busy = false
                runCatching { progress.dismiss() }
                if (ok) {
                    PreferenceManager.getDefaultSharedPreferences(context).edit()
                        .putString(KEY_LAST_ADDRESS, address).apply()
                    enteredRoom(context, hosted = false)
                } else {
                    MaterialAlertDialogBuilder(context)
                        .setTitle(R.string.multiplayer_could_not_connect)
                        .setMessage(context.getString(R.string.ds_mp_join_failed, address))
                        .setPositiveButton(android.R.string.ok, null)
                        .show()
                    onFail()
                }
            }
        }, "DsLanJoin").start()
    }

    /** Entry point from the Multiplayer dialog: the room if in one, else the room browser. */
    fun open(context: Context) {
        if (DsLan.inSession) showRoom(context) else showBrowser(context)
    }

    // ------------------------------------------------------------------ room browser

    fun showBrowser(context: Context) {
        if (DsLan.inSession) {
            showRoom(context)
            return
        }
        if (!prepareLan(context)) return

        val dialog = BottomSheetDialog(context)
        val binding = DialogLobbyBrowserBinding.inflate(LayoutInflater.from(context))
        dialog.setContentView(binding.root)
        dialog.expand(context)

        binding.title.setText(R.string.ds_mp_rooms_title)
        binding.searchText.setHint(R.string.ds_mp_search_rooms)
        binding.chipHideEmpty.setText(R.string.ds_mp_hide_in_game)
        binding.emptyRefreshButton.setText(R.string.ds_lan_join_by_ip)
        binding.emptyRefreshButton.setIconResource(R.drawable.ic_ip)
        // The empty view's texts have no ids: reach them through its children.
        (binding.emptyView.getChildAt(1) as? android.widget.TextView)
            ?.setText(R.string.ds_mp_searching_title)
        val emptyBody = binding.emptyView.getChildAt(2) as? android.widget.TextView
        binding.progressBar.visibility = View.VISIBLE
        binding.refreshButton.setIconResource(R.drawable.ic_ip)
        binding.refreshButton.contentDescription = context.getString(R.string.ds_lan_join_by_ip)

        var allRooms = emptyList<DsLan.Room>()
        lateinit var adapter: RoomAdapter
        adapter = RoomAdapter(context) { room ->
            if (room.hasPassword) {
                askPassword(context) { pw ->
                    dialog.dismiss()
                    join(context, room.address, pw) { showBrowser(context) }
                }
            } else {
                dialog.dismiss()
                join(context, room.address, "") { showBrowser(context) }
            }
        }
        binding.roomList.layoutManager = LinearLayoutManager(context)
        binding.roomList.adapter = adapter

        fun applyFilters() {
            val term = binding.searchText.text.toString().trim().lowercase(Locale.getDefault())
            adapter.submit(allRooms.filter { r ->
                (!binding.chipHideFull.isChecked || r.maxPlayers == 0 || r.players < r.maxPlayers) &&
                    (!binding.chipHideLocked.isChecked || !r.hasPassword) &&
                    (!binding.chipHideEmpty.isChecked || !r.inGame) &&
                    (term.isEmpty() || listOf(r.name, r.game, r.description, r.address)
                        .any { it.lowercase(Locale.getDefault()).contains(term) })
            })
        }
        binding.chipHideFull.setOnClickListener { applyFilters() }
        binding.chipHideLocked.setOnClickListener { applyFilters() }
        binding.chipHideEmpty.setOnClickListener { applyFilters() }
        binding.searchText.doOnTextChanged { text, _, _, _ ->
            binding.clearButton.visibility = if (text.isNullOrEmpty()) View.INVISIBLE else View.VISIBLE
            applyFilters()
        }
        binding.clearButton.setOnClickListener { binding.searchText.setText("") }

        val direct = View.OnClickListener {
            dialog.dismiss()
            showDirectConnect(context)
        }
        binding.refreshButton.setOnClickListener(direct)
        binding.emptyRefreshButton.setOnClickListener(direct)

        var signature = ""
        val refresh = object : Runnable {
            override fun run() {
                if (!DsLan.busy && !DsLan.inSession) {
                    allRooms = runCatching { DsLan.rooms() }.getOrDefault(emptyList())
                }
                val sig = allRooms.joinToString("\n")
                if (sig != signature) {
                    signature = sig
                    applyFilters()
                }
                val empty = allRooms.isEmpty()
                binding.emptyView.visibility = if (empty) View.VISIBLE else View.GONE
                binding.roomList.visibility = if (empty) View.GONE else View.VISIBLE
                binding.progressBar.visibility = if (empty) View.VISIBLE else View.GONE
                emptyBody?.text = context.getString(
                    R.string.ds_mp_searching_body,
                    localAddress() ?: context.getString(R.string.ds_mp_no_network)
                )
                handler.postDelayed(this, REFRESH_MS)
            }
        }

        acquireMulticast(context)
        DsLan.startDiscovery()
        DsLan.browsing = true
        DsLan.startPump()
        handler.post(refresh)
        dialog.setOnDismissListener {
            handler.removeCallbacks(refresh)
            DsLan.browsing = false
            if (!DsLan.inSession && !DsLan.busy) DsLan.endDiscovery()
            releaseMulticast()
        }
        dialog.show()
    }

    private fun askPassword(context: Context, onPassword: (String) -> Unit) {
        val view = LayoutInflater.from(context).inflate(R.layout.dialog_password_input, null)
        val input = view.findViewById<TextInputEditText>(R.id.password_input)
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.multiplayer_password_required)
            .setView(view)
            .setPositiveButton(R.string.multiplayer_join_room) { _, _ ->
                onPassword(input.text?.toString().orEmpty())
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private class RoomAdapter(
        private val context: Context,
        private val onClick: (DsLan.Room) -> Unit
    ) : RecyclerView.Adapter<RoomAdapter.Holder>() {
        private var rooms = emptyList<DsLan.Room>()

        class Holder(val binding: ItemLobbyRoomBinding) : RecyclerView.ViewHolder(binding.root)

        fun submit(list: List<DsLan.Room>) {
            rooms = list.sortedBy { it.name.lowercase(Locale.getDefault()) }
            notifyDataSetChanged()
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) =
            Holder(ItemLobbyRoomBinding.inflate(LayoutInflater.from(parent.context), parent, false))

        override fun onBindViewHolder(holder: Holder, position: Int) {
            val room = rooms[position]
            holder.binding.apply {
                roomName.text = room.name.ifEmpty { room.address }
                roomOwner.text = if (room.description.isNotEmpty()) {
                    context.getString(R.string.ds_mp_hosted_by, room.description, room.address)
                } else {
                    room.address
                }
                gameName.text = gameText(context, room)
                playerCount.text = context.getString(
                    R.string.multiplayer_player_count, room.players, room.maxPlayers
                )
                lockIcon.visibility = if (room.hasPassword) View.VISIBLE else View.GONE
                root.setOnClickListener { onClick(room) }
            }
        }

        override fun getItemCount() = rooms.size
    }

    // ------------------------------------------------------------------ host / join by IP

    fun showHost(context: Context) {
        if (DsLan.inSession) {
            showRoom(context)
            return
        }
        val dialog = BottomSheetDialog(context)
        val binding = DialogMultiplayerRoomBinding.inflate(LayoutInflater.from(context))
        dialog.setContentView(binding.root)
        dialog.expand(context)

        binding.textTitle.setText(R.string.ds_mp_host_title)
        // Fixed ports; the address is this device's (shown for others to join by IP).
        (binding.ipAddress.parent.parent as? View)?.visibility = View.GONE
        (binding.ipPort.parent.parent as? View)?.visibility = View.GONE
        binding.preferedGameName.visibility = View.GONE
        binding.username.setText(nickname(context))
        binding.roomName.setText(context.getString(R.string.ds_lan_default_room, nickname(context)))
        binding.maxPlayers.value = 4f
        binding.maxPlayersLabel.text =
            context.getString(R.string.multiplayer_max_players_value, 4)
        binding.maxPlayers.addOnChangeListener { _, value, _ ->
            binding.maxPlayersLabel.text =
                context.getString(R.string.multiplayer_max_players_value, value.toInt())
        }
        binding.btnConfirm.setText(R.string.multiplayer_create_room)
        binding.btnConfirm.setOnClickListener {
            val name = binding.username.text.toString().trim()
            if (!nicknameValid(name)) {
                toast(context.getString(R.string.username_invalid))
                return@setOnClickListener
            }
            if (!prepareLan(context)) return@setOnClickListener
            (context as? Activity)?.let { NetPlayManager.setUsername(it, name) }
            val roomName = binding.roomName.text.toString().trim()
                .ifEmpty { context.getString(R.string.ds_lan_default_room, name) }
            DsLan.endDiscovery()
            val ok = DsLan.host(
                name, binding.maxPlayers.value.toInt(), roomName,
                binding.password.text.toString()
            )
            if (!ok) {
                toast(context.getString(R.string.ds_lan_host_failed))
                return@setOnClickListener
            }
            dialog.dismiss()
            enteredRoom(context, hosted = true)
        }
        dialog.show()
    }

    fun showDirectConnect(context: Context) {
        if (DsLan.inSession) {
            showRoom(context)
            return
        }
        val dialog = BottomSheetDialog(context)
        val binding = DialogMultiplayerRoomBinding.inflate(LayoutInflater.from(context))
        dialog.setContentView(binding.root)
        dialog.expand(context)

        binding.textTitle.setText(R.string.ds_mp_direct_title)
        (binding.ipPort.parent.parent as? View)?.visibility = View.GONE
        binding.preferedGameName.visibility = View.GONE
        binding.roomName.visibility = View.GONE
        (binding.roomName.parent.parent as? View)?.visibility = View.GONE
        binding.maxPlayersContainer.visibility = View.GONE
        binding.ipAddress.setText(
            PreferenceManager.getDefaultSharedPreferences(context).getString(KEY_LAST_ADDRESS, "")
        )
        binding.username.setText(nickname(context))
        binding.btnConfirm.setText(R.string.multiplayer_join_room)
        binding.btnConfirm.setOnClickListener {
            val name = binding.username.text.toString().trim()
            val address = binding.ipAddress.text.toString().trim()
            if (!nicknameValid(name)) {
                toast(context.getString(R.string.username_invalid))
                return@setOnClickListener
            }
            if (address.length < 7) {
                toast(context.getString(R.string.multiplayer_input_invalid))
                return@setOnClickListener
            }
            (context as? Activity)?.let { NetPlayManager.setUsername(it, name) }
            dialog.dismiss()
            join(context, address, binding.password.text.toString()) { showDirectConnect(context) }
        }
        dialog.show()
    }

    // ------------------------------------------------------------------ room

    /** Pulls new chat and join/leave lines into [chatLog]; returns how many were added. */
    private fun syncChat(context: Context): Int {
        var added = 0
        val now = DsLan.players()
            .filter { it.status == DsLan.STATUS_HOST || it.status == DsLan.STATUS_CLIENT }
            .associate { it.id to it.name }
        lastPlayers?.let { before ->
            now.forEach { (id, n) ->
                if (id !in before) {
                    chatLog.add(ChatLine("", context.getString(R.string.ds_mp_has_joined, n), Color.GRAY, true)); added++
                }
            }
            before.forEach { (id, n) ->
                if (id !in now) {
                    chatLog.add(ChatLine("", context.getString(R.string.ds_mp_has_left, n), Color.GRAY, true)); added++
                }
            }
        }
        lastPlayers = now

        val total = runCatching { DsLan.chatTotal() }.getOrDefault(0)
        if (total < chatSeen) chatSeen = 0
        if (total > chatSeen) {
            DsLan.chatMessages().takeLast(total - chatSeen).forEach { m ->
                chatLog.add(ChatLine(m.sender, m.text, PLAYER_COLORS[m.senderId and 15], false))
                added++
            }
            chatSeen = total
        }
        while (chatLog.size > 300) chatLog.removeAt(0)
        return added
    }

    private sealed class Row {
        data class Info(val icon: Int, val text: String, val copy: String? = null) : Row()
        object Separator : Row()
        data class Member(val player: DsLan.Player) : Row()
    }

    fun showRoom(context: Context) {
        if (!DsLan.inSession) {
            showBrowser(context)
            return
        }
        val dialog = BottomSheetDialog(context)
        val binding = DialogMultiplayerLobbyBinding.inflate(LayoutInflater.from(context))
        dialog.setContentView(binding.root)
        dialog.expand(context)

        val amHost = DsLan.isHost()
        binding.btnModeration.visibility = View.GONE
        binding.btnLeave.setText(if (amHost) R.string.ds_mp_close_room else R.string.multiplayer_exit_room)

        val adapter = RoomRowAdapter(context, amHost)
        binding.listMultiplayer.layoutManager = LinearLayoutManager(context)
        binding.listMultiplayer.adapter = adapter

        var unread = 0
        var chatOpen = false
        fun updateChatButton() {
            binding.btnChat.text = if (unread > 0) {
                context.getString(R.string.ds_mp_chat_unread, unread)
            } else {
                context.getString(R.string.multiplayer_chat)
            }
        }
        binding.btnChat.setOnClickListener {
            unread = 0
            updateChatButton()
            chatOpen = true
            showChat(context) { chatOpen = false }
        }

        var signature = ""
        val refresh = object : Runnable {
            override fun run() {
                if (!DsLan.inSession) {
                    toast(context.getString(R.string.ds_mp_room_closed))
                    dialog.dismiss()
                    return
                }
                val added = syncChat(context)
                if (!chatOpen && added > 0) {
                    unread += added
                    updateChatButton()
                }
                val room = DsLan.currentRoom()
                val players = DsLan.players()
                    .filter { it.status != 0 && it.status != DsLan.STATUS_DISCONNECTED }
                val rows = mutableListOf<Row>()
                if (room != null) {
                    binding.textTitle.text = room.name
                    rows.add(Row.Info(R.drawable.ic_system, context.getString(R.string.ds_mp_room_code, room.code)))
                    rows.add(Row.Info(R.drawable.ic_joined, context.getString(R.string.multiplayer_player_count, room.players, room.maxPlayers)))
                    rows.add(Row.Info(R.drawable.ic_controller, context.getString(R.string.ds_mp_game, gameText(context, room))))
                    if (amHost) {
                        val me = localAddress()
                        if (me != null) {
                            rows.add(Row.Info(R.drawable.ic_ip, context.getString(R.string.ds_mp_host_address, me), me))
                        }
                    } else if (room.address.isNotEmpty()) {
                        val host = room.description.ifEmpty { room.address }
                        rows.add(Row.Info(R.drawable.ic_ip, context.getString(R.string.ds_mp_hosted_by, host, room.address), room.address))
                    }
                    rows.add(Row.Info(0, context.getString(R.string.ds_mp_hint)))
                }
                rows.add(Row.Separator)
                players.sortedWith(compareByDescending<DsLan.Player> { it.status == DsLan.STATUS_HOST }.thenBy { it.id })
                    .forEach { rows.add(Row.Member(it)) }
                val sig = rows.joinToString("\n") {
                    if (it is Row.Member) it.player.copy(ping = it.player.ping / 20).toString() else it.toString()
                }
                if (sig != signature) {
                    signature = sig
                    adapter.submit(rows)
                }
                handler.postDelayed(this, REFRESH_MS)
            }
        }

        binding.btnLeave.setOnClickListener {
            MaterialAlertDialogBuilder(context)
                .setMessage(if (amHost) R.string.ds_mp_close_confirm else R.string.ds_mp_leave_confirm)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    handler.removeCallbacks(refresh)
                    DsLan.leave()
                    dialog.dismiss()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        handler.post(refresh)
        dialog.setOnDismissListener { handler.removeCallbacks(refresh) }
        dialog.show()
    }

    private class RoomRowAdapter(
        private val context: Context,
        private val amHost: Boolean
    ) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
        private var rows = emptyList<Row>()

        fun submit(list: List<Row>) {
            rows = list
            notifyDataSetChanged()
        }

        override fun getItemViewType(position: Int) = when (rows[position]) {
            is Row.Info -> 0
            is Row.Separator -> 1
            is Row.Member -> 2
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            return when (viewType) {
                0 -> TextHolder(ItemTextNetplayBinding.inflate(inflater, parent, false))
                1 -> object : RecyclerView.ViewHolder(
                    inflater.inflate(R.layout.item_separator_netplay, parent, false)
                ) {}
                else -> MemberHolder(ItemButtonNetplayBinding.inflate(inflater, parent, false))
            }
        }

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            when (val row = rows[position]) {
                is Row.Info -> (holder as TextHolder).bind(row)
                is Row.Member -> (holder as MemberHolder).bind(row.player)
                Row.Separator -> {}
            }
        }

        override fun getItemCount() = rows.size

        inner class TextHolder(val binding: ItemTextNetplayBinding) : RecyclerView.ViewHolder(binding.root) {
            fun bind(row: Row.Info) {
                binding.itemTextNetplayName.text = row.text
                if (row.icon != 0) {
                    binding.itemIcon.setImageResource(row.icon)
                    binding.itemIcon.visibility = View.VISIBLE
                } else {
                    binding.itemIcon.visibility = View.INVISIBLE
                }
                if (row.copy != null) {
                    binding.root.setOnClickListener {
                        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        clipboard.setPrimaryClip(ClipData.newPlainText("address", row.copy))
                        Toast.makeText(context, R.string.ds_mp_address_copied, Toast.LENGTH_SHORT).show()
                    }
                } else {
                    binding.root.setOnClickListener(null)
                    binding.root.isClickable = false
                }
            }
        }

        inner class MemberHolder(val binding: ItemButtonNetplayBinding) : RecyclerView.ViewHolder(binding.root) {
            fun bind(player: DsLan.Player) {
                val name = if (player.isLocal) context.getString(R.string.ds_mp_you, player.name) else player.name
                val status = when (player.status) {
                    DsLan.STATUS_HOST -> context.getString(R.string.ds_mp_host_badge)
                    DsLan.STATUS_CONNECTING -> context.getString(R.string.ds_lan_status_connecting)
                    else -> null
                }
                val ping = if (player.isLocal) null else context.getString(R.string.ds_mp_ping, player.ping)
                val details = listOfNotNull(status, ping).joinToString(" · ")
                binding.itemButtonNetplayName.text =
                    if (details.isEmpty()) name else context.getString(R.string.ds_mp_member_line, name, details)
                binding.itemButtonNetplayName.setTextColor(PLAYER_COLORS[player.id and 15])
                val canKick = amHost && !player.isLocal
                binding.itemButtonMore.visibility = if (canKick) View.VISIBLE else View.GONE
                binding.itemButtonMore.setOnClickListener { view ->
                    PopupMenu(view.context, view).apply {
                        inflate(R.menu.menu_netplay_member)
                        menu.findItem(R.id.action_ban).isVisible = false
                        setOnMenuItemClickListener { item ->
                            if (item.itemId == R.id.action_kick) {
                                MaterialAlertDialogBuilder(context)
                                    .setMessage(context.getString(R.string.ds_lan_kick_confirm, player.name))
                                    .setPositiveButton(R.string.ds_lan_kick) { _, _ -> DsLan.kick(player.id) }
                                    .setNegativeButton(android.R.string.cancel, null)
                                    .show()
                                true
                            } else {
                                false
                            }
                        }
                        show()
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ chat

    private fun showChat(context: Context, onClose: () -> Unit) {
        val dialog = BottomSheetDialog(context)
        val binding = DialogChatBinding.inflate(LayoutInflater.from(context))
        dialog.setContentView(binding.root)
        dialog.expand(context)

        val adapter = ChatLineAdapter()
        binding.chatRecyclerView.layoutManager = LinearLayoutManager(context).apply { stackFromEnd = true }
        binding.chatRecyclerView.adapter = adapter
        fun redraw() {
            adapter.submit(chatLog.toList())
            binding.chatRecyclerView.scrollToPosition(adapter.itemCount - 1)
        }
        redraw()

        fun send() {
            val text = binding.chatInput.text?.toString()?.trim().orEmpty()
            if (text.isEmpty() || !DsLan.inSession) return
            DsLan.sendChat(text)
            binding.chatInput.text?.clear()
            syncChat(context)
            redraw()
        }
        binding.sendButton.setOnClickListener { send() }

        val refresh = object : Runnable {
            override fun run() {
                if (!DsLan.inSession) {
                    dialog.dismiss()
                    return
                }
                if (syncChat(context) > 0) redraw()
                handler.postDelayed(this, REFRESH_MS)
            }
        }
        handler.post(refresh)
        dialog.setOnDismissListener {
            handler.removeCallbacks(refresh)
            onClose()
        }
        dialog.show()
    }

    private class ChatLineAdapter : RecyclerView.Adapter<ChatLineAdapter.Holder>() {
        private var lines = emptyList<ChatLine>()

        class Holder(val binding: ItemChatMessageBinding) : RecyclerView.ViewHolder(binding.root)

        fun submit(list: List<ChatLine>) {
            lines = list
            notifyDataSetChanged()
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) =
            Holder(ItemChatMessageBinding.inflate(LayoutInflater.from(parent.context), parent, false))

        override fun onBindViewHolder(holder: Holder, position: Int) {
            val line = lines[position]
            holder.binding.apply {
                userIcon.setImageResource(if (line.system) R.drawable.ic_system else R.drawable.ic_user)
                if (line.system) {
                    usernameText.visibility = View.GONE
                    messageText.alpha = 0.7f
                } else {
                    usernameText.visibility = View.VISIBLE
                    usernameText.text = line.sender
                    usernameText.setTextColor(line.color)
                    messageText.alpha = 1f
                }
                messageText.text = line.text
                timestampText.text = line.time
                timestampText.alpha = 0.6f
            }
        }

        override fun getItemCount() = lines.size
    }
}
