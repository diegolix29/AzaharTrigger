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
    const size_t first =
        messages.size() > static_cast<size_t>(max_lines) ? messages.size() - max_lines : 0;
    int count = 0;
    for (size_t i = first; i < messages.size(); i++)
        std::snprintf(lines[count++], 300, "%u: %s", messages[i].SenderID, messages[i].Message);
    return count;
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
