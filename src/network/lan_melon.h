// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
using sockaddr_t = SOCKADDR;
using sockaddr_in_t = SOCKADDR_IN;
#else
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
using socket_t = int;
using sockaddr_t = struct sockaddr;
using sockaddr_in_t = struct sockaddr_in;
#define closesocket close
#endif

#include "enet/enet.h"
#include "common/common_types.h"

namespace Network {

constexpr u32 kMelonDiscoveryMagic = 0x444E414C; // "LAND"
constexpr u32 kMelonLANMagic = 0x504E414C;       // "LANP"
constexpr u32 kMelonPacketMagic = 0x4946494E;    // "NIFI"
constexpr u32 kMelonProtocolVersion = 1;

constexpr int kMelonDiscoveryPort = 7063;
constexpr int kMelonLANPort = 7064;

enum MelonPlayerStatus : u8 {
    Player_None = 0,
    Player_Client,
    Player_Host,
    Player_Connecting,
    Player_Disconnected,
};

struct MelonPlayer {
    int ID;                     // 4 bytes to match melonDS
    char Name[32];
    MelonPlayerStatus Status;   // Keep as enum but will be 4 bytes
    u32 Address;
    bool IsLocalPlayer;         // Add missing field
    u32 Ping;
};

struct MelonRoomInfo {
    char RoomCode[9];       // 8-char room code + NUL
    char RoomName[64];      // Human-readable room name
    char GameName[64];      // Game being played
    char Description[128];  // Room description
    char Password[33];       // Room password (32 + NUL), empty if no password
    u8 NumPlayers;
    u8 MaxPlayers;
    u8 HasPassword;         // 1 if password protected
    u8 InGame;              // 1 if game is running
    u32 HostID;
};

struct MelonDiscoveryData {
    u32 Magic;
    u32 Version;
    u32 Tick;
    char SessionName[64];
    u8 NumPlayers;
    u8 MaxPlayers;
    u8 Status; // 0=idle 1=playing
};

enum MelonLANCommand : u8 {
    Cmd_ClientInit = 1,
    Cmd_PlayerInfo,
    Cmd_PlayerList,
    Cmd_PlayerConnect,
    Cmd_PlayerDisconnect,
};

enum MelonLANChannel : u8 {
    Chan_Cmd = 0,
    Chan_MP = 1,
};

struct MelonPacketHeader {
    u32 Magic;     // Overwritten with timestamp when received
    u8 SenderID;
    u32 Type;      // Lower 16 bits: type, upper 16 bits: aid
    u32 Length;
    u64 Timestamp;
};

class MelonLANAdapter {
public:
    MelonLANAdapter();
    ~MelonLANAdapter();

    bool Init();
    void Shutdown();

    // Discovery
    bool StartDiscovery();
    void StopDiscovery();
    std::map<u32, MelonDiscoveryData> GetDiscoveryList();

    // Host/Client
    bool StartHost(const std::string& player_name, int max_players);
    bool StartClient(const std::string& player_name, const std::string& host_address);
    void EndSession();

    // Player management
    std::vector<MelonPlayer> GetPlayerList();
    int GetNumPlayers() const { return num_players; }
    int GetMaxPlayers() const { return max_players; }

    // Processing
    void Process();

    // MP packet interface (for DS WiFi emulation)
    int SendPacket(int inst, u8* data, int len, u64 timestamp);
    int RecvPacket(int inst, u8* data, u64* timestamp);
    int SendCmd(int inst, u8* data, int len, u64 timestamp);
    int SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid);
    int SendAck(int inst, u8* data, int len, u64 timestamp);
    int RecvHostPacket(int inst, u8* data, u64* timestamp);
    u16 RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask);

    void Begin(int inst);
    void End(int inst);

    bool IsActive() const { return active; }
    bool IsHost() const { return is_host; }

private:
    void ProcessDiscovery();
    void ProcessLAN(int type);

    void ProcessHostEvent(ENetEvent& event);
    void ProcessClientEvent(ENetEvent& event);

    void HostUpdatePlayerList();
    void ClientUpdatePlayerList();

    int SendPacketGeneric(u32 type, u8* packet, int len, u64 timestamp);
    int RecvPacketGeneric(u8* packet, bool block, u64* timestamp);

    bool inited;
    bool active;
    bool is_host;

    ENetHost* enet_host;
    ENetPeer* remote_peers[16];

    socket_t discovery_socket;
    u32 discovery_last_tick;
    std::map<u32, MelonDiscoveryData> discovery_list;
    std::mutex discovery_mutex;

    MelonPlayer players[16];
    int num_players;
    int max_players;
    std::mutex players_mutex;

    MelonPlayer my_player;
    u32 host_address;

    u16 connected_bitmask;

    int mp_recv_timeout;
    int last_host_id;
    ENetPeer* last_host_peer;
    std::queue<ENetPacket*> rx_queue;

    u32 frame_count;
};

} // namespace Network
