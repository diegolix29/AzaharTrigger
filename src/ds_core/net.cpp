// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Local wireless (melonDS MPInterface: LocalMP / LAN) and internet access
// (melonDS Net + libslirp user-mode NAT).

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "net/LAN.h"
#include "net/MPInterface.h"
#include "net/Net.h"
#include "net/Net_Slirp.h"

#include "ds_console.h"

using namespace melonDS;

namespace {

std::mutex g_net_lock;
melonDS::Net g_net;
bool g_internet_enabled = false;
azds_mp_mode g_mp_mode = AZDS_MP_NONE;

LAN* CurrentLAN() {
    if (g_mp_mode != AZDS_MP_LAN)
        return nullptr;
    return static_cast<LAN*>(&MPInterface::Get());
}

void CopyString(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0)
        return;
    std::snprintf(dst, dst_len, "%s", src ? src : "");
}

// LAN discovery keys are host-order IPv4 (first octet in the high byte).
void FormatHostOrderIPv4(char* out, size_t len, u32 addr) {
    std::snprintf(out, len, "%u.%u.%u.%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                  (addr >> 8) & 0xFF, addr & 0xFF);
}

// Player addresses are stored as in_addr (first octet in the low byte).
void FormatNetworkOrderIPv4(char* out, size_t len, u32 addr) {
    std::snprintf(out, len, "%u.%u.%u.%u", addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF,
                  (addr >> 24) & 0xFF);
}

} // namespace

namespace AzalonDS {

void NetRegisterInstance(int instance) {
    std::lock_guard guard(g_net_lock);
    g_net.RegisterInstance(instance);
}

void NetUnregisterInstance(int instance) {
    std::lock_guard guard(g_net_lock);
    g_net.UnregisterInstance(instance);
}

int NetSendPacket(u8* data, int len, int instance) {
    return g_net.SendPacket(data, len, instance);
}

int NetRecvPacket(u8* data, int instance) {
    return g_net.RecvPacket(data, instance);
}

} // namespace AzalonDS

extern "C" {

// ---------------------------------------------------------------------------
// Wireless mode

AZDS_API void azds_mp_set_mode(azds_mp_mode mode) {
    if (mode == g_mp_mode)
        return;
    switch (mode) {
    case AZDS_MP_LOCAL:
        MPInterface::Set(MPInterface_Local);
        break;
    case AZDS_MP_LAN:
        MPInterface::Set(MPInterface_LAN);
        break;
    default:
        MPInterface::Set(MPInterface_Dummy);
        mode = AZDS_MP_NONE;
        break;
    }
    g_mp_mode = mode;
}

AZDS_API azds_mp_mode azds_mp_get_mode(void) {
    return g_mp_mode;
}

AZDS_API void azds_mp_process(void) {
    MPInterface::Get().Process();
}

// ---------------------------------------------------------------------------
// LAN sessions

AZDS_API int azds_lan_start_discovery(void) {
    LAN* lan = CurrentLAN();
    return lan && lan->StartDiscovery() ? 1 : 0;
}

AZDS_API void azds_lan_end_discovery(void) {
    if (LAN* lan = CurrentLAN())
        lan->EndDiscovery();
}

AZDS_API int azds_lan_get_rooms(azds_lan_room* out, int max_rooms) {
    LAN* lan = CurrentLAN();
    if (!lan || !out || max_rooms <= 0)
        return 0;
    int count = 0;
    for (const auto& [addr, data] : lan->GetDiscoveryList()) {
        if (count >= max_rooms)
            break;
        azds_lan_room& room = out[count++];
        std::memset(&room, 0, sizeof(room));
        FormatHostOrderIPv4(room.address, sizeof(room.address), addr);
        CopyString(room.room_code, sizeof(room.room_code), data.Room.RoomCode);
        CopyString(room.room_name, sizeof(room.room_name), data.Room.RoomName);
        CopyString(room.game_name, sizeof(room.game_name), data.Room.GameName);
        CopyString(room.description, sizeof(room.description), data.Room.Description);
        room.num_players = data.Room.NumPlayers;
        room.max_players = data.Room.MaxPlayers;
        room.has_password = data.Room.HasPassword;
        room.in_game = data.Room.InGame;
    }
    return count;
}

AZDS_API int azds_lan_host(const char* player_name, int max_players, const char* room_name,
                           const char* password) {
    LAN* lan = CurrentLAN();
    if (!lan || !player_name)
        return 0;
    const char* pass = (password && *password) ? password : nullptr;
    return lan->StartHost(player_name, max_players, room_name ? room_name : "", pass) ? 1 : 0;
}

AZDS_API int azds_lan_join(const char* player_name, const char* host_address,
                           const char* password) {
    LAN* lan = CurrentLAN();
    if (!lan || !player_name || !host_address)
        return 0;
    const char* pass = (password && *password) ? password : nullptr;
    return lan->StartClient(player_name, host_address, pass) ? 1 : 0;
}

AZDS_API void azds_lan_leave(void) {
    if (LAN* lan = CurrentLAN())
        lan->EndSession();
}

AZDS_API int azds_lan_get_players(azds_lan_player* out, int max_players) {
    LAN* lan = CurrentLAN();
    if (!lan || !out || max_players <= 0)
        return 0;
    int count = 0;
    for (const auto& player : lan->GetPlayerList()) {
        if (count >= max_players)
            break;
        azds_lan_player& p = out[count++];
        std::memset(&p, 0, sizeof(p));
        p.id = player.ID;
        CopyString(p.name, sizeof(p.name), player.Name);
        p.status = static_cast<int>(player.Status);
        FormatNetworkOrderIPv4(p.address, sizeof(p.address), player.Address);
        p.is_local = player.IsLocalPlayer ? 1 : 0;
        p.ping = player.Ping;
    }
    return count;
}

AZDS_API int azds_lan_in_session(void) {
    LAN* lan = CurrentLAN();
    return lan && lan->IsInSession() ? 1 : 0;
}

AZDS_API int azds_lan_num_players(void) {
    LAN* lan = CurrentLAN();
    return lan ? lan->GetNumPlayers() : 0;
}

AZDS_API int azds_lan_max_players(void) {
    LAN* lan = CurrentLAN();
    return lan ? lan->GetMaxPlayers() : 0;
}

AZDS_API void azds_lan_send_chat(const char* message) {
    LAN* lan = CurrentLAN();
    if (lan && message && *message)
        lan->SendChatMessage(message);
}

AZDS_API int azds_lan_get_chat(char (*lines)[300], int max_lines) {
    LAN* lan = CurrentLAN();
    if (!lan || !lines || max_lines <= 0)
        return 0;
    const auto messages = lan->GetChatMessages();
    const auto players = lan->GetPlayerList();
    const size_t first =
        messages.size() > static_cast<size_t>(max_lines) ? messages.size() - max_lines : 0;
    int count = 0;
    for (size_t i = first; i < messages.size(); i++) {
        const char* sender = nullptr;
        for (const auto& player : players) {
            if (static_cast<u32>(player.ID) == messages[i].SenderID && player.Name[0]) {
                sender = player.Name;
                break;
            }
        }
        if (sender)
            std::snprintf(lines[count++], 300, "%s: %s", sender, messages[i].Message);
        else
            std::snprintf(lines[count++], 300, "Player %u: %s", messages[i].SenderID + 1,
                          messages[i].Message);
    }
    return count;
}

AZDS_API int azds_lan_get_chat_messages(azds_lan_chat* out, int max_messages) {
    LAN* lan = CurrentLAN();
    if (!lan || !out || max_messages <= 0)
        return 0;
    const auto messages = lan->GetChatMessages();
    const auto players = lan->GetPlayerList();
    int local_id = -1;
    for (const auto& player : players)
        if (player.IsLocalPlayer)
            local_id = player.ID;
    const size_t first =
        messages.size() > static_cast<size_t>(max_messages) ? messages.size() - max_messages : 0;
    int count = 0;
    for (size_t i = first; i < messages.size(); i++) {
        const auto& m = messages[i];
        azds_lan_chat& c = out[count++];
        std::memset(&c, 0, sizeof(c));
        c.sender_id = static_cast<int>(m.SenderID);
        if (m.SenderName[0])
            CopyString(c.sender, sizeof(c.sender), m.SenderName);
        else
            std::snprintf(c.sender, sizeof(c.sender), "Player %u", m.SenderID + 1);
        c.is_local = static_cast<int>(m.SenderID) == local_id ? 1 : 0;
        CopyString(c.text, sizeof(c.text), m.Message);
    }
    return count;
}

AZDS_API unsigned azds_lan_chat_total(void) {
    LAN* lan = CurrentLAN();
    return lan ? lan->GetChatTotal() : 0;
}

AZDS_API int azds_lan_is_host(void) {
    LAN* lan = CurrentLAN();
    return lan && lan->IsHosting() ? 1 : 0;
}

AZDS_API int azds_lan_get_current_room(azds_lan_room* out) {
    LAN* lan = CurrentLAN();
    if (!lan || !out || !lan->IsInSession())
        return 0;
    const LAN::RoomInfo info = lan->GetRoomInfo();
    std::memset(out, 0, sizeof(*out));
    if (!lan->IsHosting())
        FormatNetworkOrderIPv4(out->address, sizeof(out->address), lan->GetHostAddress());
    CopyString(out->room_code, sizeof(out->room_code), info.RoomCode);
    CopyString(out->room_name, sizeof(out->room_name), info.RoomName);
    CopyString(out->game_name, sizeof(out->game_name), info.GameName);
    CopyString(out->description, sizeof(out->description), info.Description);
    out->num_players = lan->GetNumPlayers();
    out->max_players = info.MaxPlayers ? info.MaxPlayers : lan->GetMaxPlayers();
    out->has_password = info.HasPassword;
    out->in_game = info.InGame;
    return 1;
}

AZDS_API void azds_lan_set_game(const char* title, int in_game) {
    if (LAN* lan = CurrentLAN())
        lan->SetRoomGame(title, in_game != 0);
}

AZDS_API int azds_lan_local_address(char* out, int out_len) {
    if (!out || out_len <= 0)
        return 0;
    out[0] = 0;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 0;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const bool valid = sock != INVALID_SOCKET;
#else
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const bool valid = sock >= 0;
#endif
    int ok = 0;
    if (valid) {
        // Connecting a UDP socket sends nothing; it only picks the outgoing interface.
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(53);
        remote.sin_addr.s_addr = htonl(0x08080808);
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0 &&
            getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) == 0 &&
            local.sin_addr.s_addr != 0) {
            FormatNetworkOrderIPv4(out, static_cast<size_t>(out_len), local.sin_addr.s_addr);
            ok = 1;
        }
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return ok;
}

AZDS_API int azds_lan_kick(int player_id) {
    LAN* lan = CurrentLAN();
    return lan && lan->KickPlayer(player_id) ? 1 : 0;
}

AZDS_API int azds_lan_ban(int player_id) {
    LAN* lan = CurrentLAN();
    return lan && lan->BanPlayer(player_id) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Internet

AZDS_API int azds_internet_enable(int enable) {
    std::lock_guard guard(g_net_lock);
    if (!enable) {
        g_net.SetDriver(nullptr);
        g_internet_enabled = false;
        return 1;
    }
    if (g_internet_enabled)
        return 1;
    g_net.SetDriver(std::make_unique<Net_Slirp>(
        [](const u8* data, int len) { g_net.RXEnqueue(data, len); }));
    g_internet_enabled = true;
    return 1;
}

AZDS_API int azds_internet_is_enabled(void) {
    return g_internet_enabled ? 1 : 0;
}

} // extern "C"
