// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "lan_melon.h"
#include "common/logging/log.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#define closesocket close
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (socket_t)-1
#endif

namespace Network {

MelonLANAdapter::MelonLANAdapter()
    : inited(false), active(false), is_host(false), enet_host(nullptr),
      discovery_socket(INVALID_SOCKET), discovery_last_tick(0), num_players(0),
      max_players(0), host_address(0), connected_bitmask(0), mp_recv_timeout(100),
      last_host_id(-1), last_host_peer(nullptr), frame_count(0) {
    std::memset(remote_peers, 0, sizeof(remote_peers));
    std::memset(players, 0, sizeof(players));
    std::memset(&my_player, 0, sizeof(my_player));
}

MelonLANAdapter::~MelonLANAdapter() {
    Shutdown();
}

bool MelonLANAdapter::Init() {
    if (enet_initialize() != 0) {
        LOG_ERROR(Network, "Failed to initialize ENet for melonDS LAN adapter");
        return false;
    }

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        LOG_ERROR(Network, "Failed to initialize Winsock for melonDS LAN adapter");
        enet_deinitialize();
        return false;
    }
#endif

    inited = true;
    LOG_INFO(Network, "MelonDS LAN adapter initialized");
    return true;
}

void MelonLANAdapter::Shutdown() {
    EndSession();
    StopDiscovery();

    if (inited) {
        enet_deinitialize();
#ifdef _WIN32
        WSACleanup();
#endif
        inited = false;
    }
}

bool MelonLANAdapter::StartDiscovery() {
    if (!inited) {
        return false;
    }

    discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket == INVALID_SOCKET) {
        LOG_ERROR(Network, "Failed to create discovery socket");
        return false;
    }
    LOG_INFO(Network, "Created discovery socket");

    sockaddr_in_t saddr;
    std::memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(kMelonDiscoveryPort);

    if (bind(discovery_socket, (const sockaddr_t*)&saddr, sizeof(saddr)) < 0) {
        LOG_ERROR(Network, "Failed to bind discovery socket to port {}", kMelonDiscoveryPort);
        closesocket(discovery_socket);
        discovery_socket = INVALID_SOCKET;
        return false;
    }
    LOG_INFO(Network, "Bound discovery socket to port {}", kMelonDiscoveryPort);

    int opt_true = 1;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt_true,
                   sizeof(int)) < 0) {
        LOG_ERROR(Network, "Failed to set SO_BROADCAST on discovery socket");
        closesocket(discovery_socket);
        discovery_socket = INVALID_SOCKET;
        return false;
    }
    LOG_INFO(Network, "Enabled broadcast on discovery socket");

    discovery_last_tick = static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count());
    discovery_list.clear();

    return true;
}

void MelonLANAdapter::StopDiscovery() {
    if (discovery_socket != INVALID_SOCKET) {
        closesocket(discovery_socket);
        discovery_socket = INVALID_SOCKET;
    }
}

std::map<u32, MelonDiscoveryData> MelonLANAdapter::GetDiscoveryList() {
    std::lock_guard<std::mutex> lock(discovery_mutex);
    return discovery_list;
}

bool MelonLANAdapter::StartHost(const std::string& player_name, int max_players_param) {
    if (!inited) {
        return false;
    }
    if (max_players_param > 16) {
        return false;
    }

    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = kMelonLANPort;

    enet_host = enet_host_create(&addr, 16, 2, 0, 0);
    if (!enet_host) {
        LOG_ERROR(Network, "Failed to create ENet host for melonDS LAN");
        return false;
    }

    std::lock_guard<std::mutex> lock(players_mutex);

    MelonPlayer* player = &players[0];
    std::memset(player, 0, sizeof(MelonPlayer));
    player->ID = 0;
    std::strncpy(player->Name, player_name.c_str(), 31);
    player->Name[31] = '\0';
    player->Status = Player_Host;
    player->Address = 0;
    player->IsLocalPlayer = true;
    player->Ping = 0;
    num_players = 1;
    max_players = max_players_param;
    std::memcpy(&my_player, player, sizeof(MelonPlayer));

    host_address = 0x0100007F;
    last_host_id = -1;
    last_host_peer = nullptr;

    active = true;
    is_host = true;

    StartDiscovery();
    LOG_INFO(Network, "MelonDS LAN host started on port {}", kMelonLANPort);
    return true;
}

bool MelonLANAdapter::StartClient(const std::string& player_name,
                                   const std::string& host_address_str) {
    if (!inited) {
        return false;
    }

    LOG_INFO(Network, "Starting melonDS LAN client connection to {}:{}", host_address_str, kMelonLANPort);

    enet_host = enet_host_create(nullptr, 16, 2, 0, 0);
    if (!enet_host) {
        LOG_ERROR(Network, "Failed to create ENet client host for melonDS LAN");
        return false;
    }

    ENetAddress addr;
    if (enet_address_set_host(&addr, host_address_str.c_str()) != 0) {
        LOG_ERROR(Network, "Failed to resolve host address: {}", host_address_str);
        enet_host_destroy(enet_host);
        enet_host = nullptr;
        return false;
    }
    addr.port = kMelonLANPort;

    ENetPeer* peer = enet_host_connect(enet_host, &addr, 2, 0);
    if (!peer) {
        LOG_ERROR(Network, "Failed to connect to melonDS LAN host");
        enet_host_destroy(enet_host);
        enet_host = nullptr;
        return false;
    }

    std::lock_guard<std::mutex> lock(players_mutex);

    MelonPlayer* player = &my_player;
    std::memset(player, 0, sizeof(MelonPlayer));
    player->ID = 0;
    std::strncpy(player->Name, player_name.c_str(), 31);
    player->Name[31] = '\0';
    player->Status = Player_Connecting;
    player->IsLocalPlayer = true;

    // Wait for connection and initialization
    ENetEvent event;
    int conn = 0;
    auto start_time = std::chrono::steady_clock::now();
    const int conn_timeout = 5000; // 5 seconds

    while (conn != 2) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time)
                           .count();
        if (elapsed >= conn_timeout) {
            break;
        }

        int timeout = conn_timeout - static_cast<int>(elapsed);
        if (enet_host_service(enet_host, &event, timeout) > 0) {
            if (conn == 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
                LOG_INFO(Network, "Connected to melonDS LAN host");
                conn = 1;
            } else if (conn == 1 && event.type == ENET_EVENT_TYPE_RECEIVE) {
                u8* data = event.packet->data;
                if (event.channelID != Chan_Cmd) {
                    continue;
                }
                LOG_INFO(Network, "Received command {} from host", data[0]);
                if (data[0] != Cmd_ClientInit) {
                    continue;
                }
                if (event.packet->dataLength != 11) {
                    continue;
                }

                u32 magic = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
                u32 version = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
                LOG_INFO(Network, "Host magic: 0x{:08X}, version: {}, expected magic: 0x{:08X}, version: {}", 
                         magic, version, kMelonLANMagic, kMelonProtocolVersion);
                if (magic != kMelonLANMagic || version != kMelonProtocolVersion) {
                    LOG_ERROR(Network, "Protocol mismatch with melonDS LAN host");
                    enet_peer_disconnect(peer, 0);
                    break;
                }
                if (data[10] > 16) {
                    break;
                }

                max_players = data[10];
                my_player.ID = data[9];
                LOG_INFO(Network, "Assigned player ID: {}, max players: {}", my_player.ID, max_players);

                // Send player info
                u8 cmd[9 + sizeof(MelonPlayer)];
                cmd[0] = Cmd_PlayerInfo;
                cmd[1] = static_cast<u8>(kMelonLANMagic);
                cmd[2] = static_cast<u8>(kMelonLANMagic >> 8);
                cmd[3] = static_cast<u8>(kMelonLANMagic >> 16);
                cmd[4] = static_cast<u8>(kMelonLANMagic >> 24);
                cmd[5] = static_cast<u8>(kMelonProtocolVersion);
                cmd[6] = static_cast<u8>(kMelonProtocolVersion >> 8);
                cmd[7] = static_cast<u8>(kMelonProtocolVersion >> 16);
                cmd[8] = static_cast<u8>(kMelonProtocolVersion >> 24);
                std::memcpy(&cmd[9], &my_player, sizeof(MelonPlayer));
                ENetPacket* pkt =
                    enet_packet_create(cmd, 9 + sizeof(MelonPlayer), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, Chan_Cmd, pkt);
                LOG_INFO(Network, "Sent player info to host");

                conn = 2;
                break;
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                LOG_ERROR(Network, "Disconnected from melonDS LAN host during connection");
                conn = 0;
                break;
            }
        } else {
            break;
        }
    }

    if (conn != 2) {
        LOG_ERROR(Network, "Failed to complete melonDS LAN connection (conn state: {})", conn);
        enet_peer_reset(peer);
        enet_host_destroy(enet_host);
        enet_host = nullptr;
        return false;
    }

    host_address = addr.host;
    last_host_id = -1;
    last_host_peer = nullptr;
    remote_peers[0] = peer;
    peer->data = &players[0];

    active = true;
    is_host = false;
    LOG_INFO(Network, "MelonDS LAN client successfully connected to {} with player ID {}", host_address_str, my_player.ID);
    return true;
}

void MelonLANAdapter::EndSession() {
    if (!active) {
        return;
    }
    if (is_host) {
        StopDiscovery();
    }

    active = false;

    while (!rx_queue.empty()) {
        ENetPacket* packet = rx_queue.front();
        rx_queue.pop();
        enet_packet_destroy(packet);
    }

    for (int i = 0; i < 16; i++) {
        if (i == my_player.ID) {
            continue;
        }

        if (remote_peers[i]) {
            enet_peer_disconnect(remote_peers[i], 0);
        }

        remote_peers[i] = nullptr;
    }

    if (enet_host) {
        enet_host_destroy(enet_host);
        enet_host = nullptr;
    }
    is_host = false;
}

std::vector<MelonPlayer> MelonLANAdapter::GetPlayerList() {
    std::lock_guard<std::mutex> lock(players_mutex);

    std::vector<MelonPlayer> ret;
    for (int i = 0; i < 16; i++) {
        if (players[i].Status == Player_None) {
            continue;
        }

        MelonPlayer newp = players[i];
        if (newp.ID == my_player.ID) {
            newp.Address = 0x0100007F; // localhost
        } else if (newp.Status == Player_Host) {
            newp.Address = host_address;
        }

        ret.push_back(newp);
    }

    return ret;
}

void MelonLANAdapter::ProcessDiscovery() {
    if (discovery_socket == INVALID_SOCKET) {
        return;
    }

    u32 tick = static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
    if ((tick - discovery_last_tick) < 1000) {
        return;
    }

    discovery_last_tick = tick;

    if (is_host) {
        // Broadcast discovery beacon
        MelonDiscoveryData beacon;
        std::memset(&beacon, 0, sizeof(beacon));
        beacon.Magic = kMelonDiscoveryMagic;
        beacon.Version = kMelonProtocolVersion;
        beacon.Tick = tick;
        
        // Fill RoomInfo
        std::snprintf(beacon.Room.RoomName, 64, "%s's game", my_player.Name);
        std::snprintf(beacon.Room.GameName, 64, "Nintendo DS");
        std::memset(beacon.Room.RoomCode, 0, 9);
        std::memset(beacon.Room.Description, 0, 128);
        std::memset(beacon.Room.Password, 0, 33);
        beacon.Room.NumPlayers = static_cast<u8>(num_players);
        beacon.Room.MaxPlayers = static_cast<u8>(max_players);
        beacon.Room.HasPassword = 0;
        beacon.Room.InGame = 0;
        beacon.Room.HostID = my_player.ID;

        sockaddr_in_t saddr;
        std::memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        saddr.sin_port = htons(kMelonDiscoveryPort);

        int sent = sendto(discovery_socket, (const char*)&beacon, sizeof(beacon), 0,
                          (const sockaddr_t*)&saddr, sizeof(saddr));
        LOG_INFO(Network, "Broadcasting discovery beacon: '{}' ({}/{} players), sent {} bytes", 
                 beacon.Room.RoomName, beacon.Room.NumPlayers, beacon.Room.MaxPlayers, sent);
    } else {
        std::lock_guard<std::mutex> lock(discovery_mutex);

        // Listen for discovery beacons
        fd_set fd;
        struct timeval tv;
        while (true) {
            FD_ZERO(&fd);
            FD_SET(discovery_socket, &fd);
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            if (!select(discovery_socket + 1, &fd, nullptr, nullptr, &tv)) {
                break;
            }

            MelonDiscoveryData beacon;
            sockaddr_in_t raddr;
            socklen_t ralen = sizeof(raddr);

            int rlen = recvfrom(discovery_socket, (char*)&beacon, sizeof(beacon), 0,
                                (sockaddr_t*)&raddr, &ralen);
            if (rlen < static_cast<int>(sizeof(beacon))) {
                continue;
            }
            LOG_INFO(Network, "Received discovery beacon: {} bytes, magic=0x{:08X}, version={}", 
                     rlen, beacon.Magic, beacon.Version);
            if (beacon.Magic != kMelonDiscoveryMagic) {
                LOG_ERROR(Network, "Invalid discovery magic: 0x{:08X}, expected 0x{:08X}", 
                         beacon.Magic, kMelonDiscoveryMagic);
                continue;
            }
            if (beacon.Version != kMelonProtocolVersion) {
                LOG_ERROR(Network, "Invalid discovery version: {}, expected {}", 
                         beacon.Version, kMelonProtocolVersion);
                continue;
            }
            if (beacon.Room.MaxPlayers > 16) {
                LOG_ERROR(Network, "Invalid max players: {}", beacon.Room.MaxPlayers);
                continue;
            }
            if (beacon.Room.NumPlayers > beacon.Room.MaxPlayers) {
                LOG_ERROR(Network, "Invalid player count: {}/{}", beacon.Room.NumPlayers, beacon.Room.MaxPlayers);
                continue;
            }

            u32 key = ntohl(raddr.sin_addr.s_addr);
            beacon.Room.RoomName[63] = '\0';
            beacon.Room.GameName[63] = '\0';
            LOG_INFO(Network, "Found host: '{}' ({}) {}/{} players, from {:08X}", 
                     beacon.Room.RoomName, beacon.Room.GameName, beacon.Room.NumPlayers, beacon.Room.MaxPlayers, key);

            auto it = discovery_list.find(key);
            if (it != discovery_list.end()) {
                if (beacon.Tick <= it->second.Tick) {
                    LOG_INFO(Network, "Ignoring old beacon from {:08X}", key);
                    continue;
                }
            }

            beacon.Magic = tick; // Store receive time
            discovery_list[key] = beacon;
        }

        // Cleanup old entries
        std::vector<u32> deletelist;
        for (const auto& [key, data] : discovery_list) {
            u32 age = tick - data.Magic;
            if (age < 5000) {
                continue;
            }
            deletelist.push_back(key);
        }

        for (const auto& key : deletelist) {
            discovery_list.erase(key);
        }
    }
}

void MelonLANAdapter::HostUpdatePlayerList() {
    LOG_INFO(Network, "Broadcasting player list update to {} players", num_players);
    u8 cmd[2 + sizeof(players)];
    cmd[0] = Cmd_PlayerList;
    cmd[1] = static_cast<u8>(num_players);
    std::memcpy(&cmd[2], players, sizeof(players));
    ENetPacket* pkt = enet_packet_create(cmd, 2 + sizeof(players), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(enet_host, Chan_Cmd, pkt);
}

void MelonLANAdapter::ClientUpdatePlayerList() {
    // Client doesn't broadcast player list
}

void MelonLANAdapter::ProcessHostEvent(ENetEvent& event) {
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT: {
        LOG_INFO(Network, "New client connecting to melonDS LAN host");
        if ((num_players >= max_players) || (num_players >= 16)) {
            LOG_ERROR(Network, "Host full, rejecting connection (players: {}/{})", num_players, max_players);
            enet_peer_disconnect(event.peer, 0);
            break;
        }

        int id;
        for (id = 0; id < 16; id++) {
            if (id >= num_players) {
                break;
            }
            if (players[id].Status == Player_None) {
                break;
            }
        }

        if (id < 16) {
            LOG_INFO(Network, "Assigning player ID {} to new client", id);
            u8 cmd[11];
            cmd[0] = Cmd_ClientInit;
            cmd[1] = static_cast<u8>(kMelonLANMagic);
            cmd[2] = static_cast<u8>(kMelonLANMagic >> 8);
            cmd[3] = static_cast<u8>(kMelonLANMagic >> 16);
            cmd[4] = static_cast<u8>(kMelonLANMagic >> 24);
            cmd[5] = static_cast<u8>(kMelonProtocolVersion);
            cmd[6] = static_cast<u8>(kMelonProtocolVersion >> 8);
            cmd[7] = static_cast<u8>(kMelonProtocolVersion >> 16);
            cmd[8] = static_cast<u8>(kMelonProtocolVersion >> 24);
            cmd[9] = static_cast<u8>(id);
            cmd[10] = static_cast<u8>(max_players);
            ENetPacket* pkt = enet_packet_create(cmd, 11, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(event.peer, Chan_Cmd, pkt);
            LOG_INFO(Network, "Sent ClientInit command to player {}", id);

            std::lock_guard<std::mutex> lock(players_mutex);

            players[id].ID = id;
            players[id].Status = Player_Connecting;
            players[id].Address = event.peer->address.host;
            players[id].IsLocalPlayer = false;
            event.peer->data = &players[id];
            num_players++;

            remote_peers[id] = event.peer;
        } else {
            LOG_ERROR(Network, "No available player slots, rejecting connection");
            enet_peer_disconnect(event.peer, 0);
        }
    } break;

    case ENET_EVENT_TYPE_DISCONNECT: {
        MelonPlayer* player = static_cast<MelonPlayer*>(event.peer->data);
        if (!player) {
            break;
        }

        connected_bitmask &= ~(1 << player->ID);

        int id = player->ID;
        remote_peers[id] = nullptr;

        player->ID = 0;
        player->Status = Player_None;
        num_players--;

        HostUpdatePlayerList();
    } break;

    case ENET_EVENT_TYPE_RECEIVE: {
        if (event.packet->dataLength < 1) {
            break;
        }

        u8* data = static_cast<u8*>(event.packet->data);
        switch (data[0]) {
        case Cmd_PlayerInfo: {
            LOG_INFO(Network, "Received PlayerInfo command from client");
            if (event.packet->dataLength != (9 + sizeof(MelonPlayer))) {
                LOG_ERROR(Network, "Invalid PlayerInfo packet size: {}", event.packet->dataLength);
                break;
            }

            u32 magic = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
            u32 version = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);
            if ((magic != kMelonLANMagic) || (version != kMelonProtocolVersion)) {
                LOG_ERROR(Network, "Invalid magic/version in PlayerInfo: 0x{:08X}/{}, expected 0x{:08X}/{}", 
                         magic, version, kMelonLANMagic, kMelonProtocolVersion);
                enet_peer_disconnect(event.peer, 0);
                break;
            }

            MelonPlayer player;
            std::memcpy(&player, &data[9], sizeof(MelonPlayer));
            player.Name[31] = '\0';

            MelonPlayer* hostside = static_cast<MelonPlayer*>(event.peer->data);
            if (player.ID != hostside->ID) {
                LOG_ERROR(Network, "Player ID mismatch: {} != {}", player.ID, hostside->ID);
                enet_peer_disconnect(event.peer, 0);
                break;
            }

            std::lock_guard<std::mutex> lock(players_mutex);

            player.Status = Player_Client;
            player.Address = event.peer->address.host;
            std::memcpy(hostside, &player, sizeof(MelonPlayer));
            LOG_INFO(Network, "Player '{}' (ID: {}) joined the session", player.Name, player.ID);

            HostUpdatePlayerList();
        } break;

        case Cmd_PlayerConnect: {
            if (event.packet->dataLength != 1) {
                break;
            }
            MelonPlayer* player = static_cast<MelonPlayer*>(event.peer->data);
            if (!player) {
                break;
            }

            connected_bitmask |= (1 << player->ID);
        } break;

        case Cmd_PlayerDisconnect: {
            if (event.packet->dataLength != 1) {
                break;
            }
            MelonPlayer* player = static_cast<MelonPlayer*>(event.peer->data);
            if (!player) {
                break;
            }

            connected_bitmask &= ~(1 << player->ID);
        } break;
        }

        enet_packet_destroy(event.packet);
    } break;

    case ENET_EVENT_TYPE_NONE:
        break;
    }
}

void MelonLANAdapter::ProcessClientEvent(ENetEvent& event) {
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT: {
        // Direct connection from another client
        int playerid = -1;
        for (int i = 0; i < 16; i++) {
            MelonPlayer* player = &players[i];
            if (i == my_player.ID) {
                continue;
            }
            if (player->Status != Player_Client) {
                continue;
            }

            if (player->Address == event.peer->address.host) {
                playerid = i;
                break;
            }
        }

        if (playerid < 0) {
            enet_peer_disconnect(event.peer, 0);
            break;
        }

        remote_peers[playerid] = event.peer;
        event.peer->data = &players[playerid];
    } break;

    case ENET_EVENT_TYPE_DISCONNECT: {
        MelonPlayer* player = static_cast<MelonPlayer*>(event.peer->data);
        if (!player) {
            break;
        }

        connected_bitmask &= ~(1 << player->ID);

        int id = player->ID;
        remote_peers[id] = nullptr;

        std::lock_guard<std::mutex> lock(players_mutex);
        player->Status = Player_Disconnected;

        ClientUpdatePlayerList();
    } break;

    case ENET_EVENT_TYPE_RECEIVE: {
        if (event.packet->dataLength < 1) {
            break;
        }

        u8* data = static_cast<u8*>(event.packet->data);
        switch (data[0]) {
        case Cmd_PlayerList: {
            LOG_INFO(Network, "Received PlayerList command from host");
            if (event.packet->dataLength != (2 + sizeof(players))) {
                LOG_ERROR(Network, "Invalid PlayerList packet size: {}", event.packet->dataLength);
                break;
            }
            if (data[1] > 16) {
                LOG_ERROR(Network, "Invalid player count in PlayerList: {}", data[1]);
                break;
            }

            std::lock_guard<std::mutex> lock(players_mutex);

            num_players = data[1];
            std::memcpy(players, &data[2], sizeof(players));
            for (int i = 0; i < 16; i++) {
                players[i].Name[31] = '\0';
            }
            LOG_INFO(Network, "Updated player list with {} players", num_players);
            for (int i = 0; i < 16; i++) {
                if (players[i].Status != Player_None) {
                    LOG_INFO(Network, "Player {}: '{}' (ID: {}, Status: {})", i, players[i].Name, players[i].ID, static_cast<int>(players[i].Status));
                }
            }

            // Send Cmd_PlayerConnect to host to confirm connection
            if (last_host_peer) {
                u8 cmd[1];
                cmd[0] = Cmd_PlayerConnect;
                ENetPacket* pkt = enet_packet_create(cmd, 1, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(last_host_peer, Chan_Cmd, pkt);
                LOG_INFO(Network, "Sent Cmd_PlayerConnect to host");
            }

            // Establish connections to new clients
            for (int i = 0; i < 16; i++) {
                MelonPlayer* player = &players[i];
                if (i == my_player.ID) {
                    continue;
                }
                if (player->Status != Player_Client) {
                    continue;
                }

                if (!remote_peers[i]) {
                    LOG_INFO(Network, "Attempting to connect to client {} at address {:08X}", i, player->Address);
                    ENetAddress peeraddr;
                    peeraddr.host = player->Address;
                    peeraddr.port = kMelonLANPort;
                    ENetPeer* peer = enet_host_connect(enet_host, &peeraddr, 2, 0);
                    if (!peer) {
                        LOG_ERROR(Network, "Failed to connect to client {}", i);
                        continue;
                    }
                }
            }
        } break;

        case Cmd_PlayerConnect: {
            if (event.packet->dataLength != 1) {
                break;
            }
            MelonPlayer* player = static_cast<MelonPlayer*>(event.peer->data);
            if (!player) {
                break;
            }

            connected_bitmask |= (1 << player->ID);
        } break;

        case Cmd_PlayerDisconnect: {
            if (event.packet->dataLength != 1) {
                break;
            }
            MelonPlayer* player = static_cast<MelonPlayer*>(event.peer->data);
            if (!player) {
                break;
            }

            connected_bitmask &= ~(1 << player->ID);
        } break;
        }

        enet_packet_destroy(event.packet);
    } break;

    case ENET_EVENT_TYPE_NONE:
        break;
    }
}

void MelonLANAdapter::ProcessLAN(int type) {
    if (!enet_host) {
        return;
    }

    u32 time_last = static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());

    // Process stale packets
    while (!rx_queue.empty()) {
        ENetPacket* enetpacket = rx_queue.front();
        MelonPacketHeader* header = reinterpret_cast<MelonPacketHeader*>(&enetpacket->data[0]);
        u32 packettime = header->Magic;

        if ((packettime > time_last) || (packettime < (time_last - 100))) {
            rx_queue.pop();
            enet_packet_destroy(enetpacket);
        } else {
            if (type == 2) {
                return;
            }
            if (type == 1) {
                if (header->Type == 0) {
                    return;
                }
                rx_queue.pop();
                enet_packet_destroy(enetpacket);
            }
            break;
        }
    }

    int timeout = (type == 2) ? mp_recv_timeout : (type == 0 ? 1 : 0);
    time_last = static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());

    int maxMPPackets = (type == 0) ? 4 : 1;
    int receivedMP = 0;

    ENetEvent event;
    while (enet_host_service(enet_host, &event, timeout) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE && event.channelID == Chan_MP) {
            MelonPacketHeader* header = reinterpret_cast<MelonPacketHeader*>(&event.packet->data[0]);

            bool good = true;
            if (event.packet->dataLength < sizeof(MelonPacketHeader)) {
                good = false;
            } else if (header->Magic != kMelonPacketMagic) {
                good = false;
            } else if (header->SenderID == my_player.ID) {
                good = false;
            }

            if (!good) {
                enet_packet_destroy(event.packet);
            } else {
                header->Magic = static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now().time_since_epoch())
                                                      .count());

                event.packet->userData = event.peer;
                rx_queue.push(event.packet);

                receivedMP++;
                if (type != 0 || receivedMP >= maxMPPackets) {
                    return;
                }
            }
        } else {
            if (is_host) {
                ProcessHostEvent(event);
            } else {
                ProcessClientEvent(event);
            }
        }

        if (type == 2) {
            u32 time = static_cast<u32>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
            if (time < time_last) {
                return;
            }
            timeout -= static_cast<int>(time - time_last);
            if (timeout <= 0) {
                return;
            }
            time_last = time;
        }
    }
}

void MelonLANAdapter::Process() {
    if (!active) {
        return;
    }

    ProcessDiscovery();
    ProcessLAN(0);

    frame_count++;
    if (frame_count >= 60) {
        frame_count = 0;

        std::lock_guard<std::mutex> lock(players_mutex);

        for (int i = 0; i < 16; i++) {
            if (players[i].Status == Player_None) {
                continue;
            }
            if (i == my_player.ID) {
                continue;
            }
            if (!remote_peers[i]) {
                continue;
            }

            players[i].Ping = remote_peers[i]->roundTripTime;
        }
    }
}

int MelonLANAdapter::SendPacketGeneric(u32 type, u8* packet, int len, u64 timestamp) {
    if (!enet_host) {
        return 0;
    }

    u32 flags = ENET_PACKET_FLAG_RELIABLE;

    ENetPacket* enetpacket =
        enet_packet_create(nullptr, sizeof(MelonPacketHeader) + len, flags);

    MelonPacketHeader pktheader;
    pktheader.Magic = kMelonPacketMagic;
    pktheader.SenderID = my_player.ID;
    pktheader.Type = type;
    pktheader.Length = len;
    pktheader.Timestamp = timestamp;
    std::memcpy(&enetpacket->data[0], &pktheader, sizeof(MelonPacketHeader));
    if (len) {
        std::memcpy(&enetpacket->data[sizeof(MelonPacketHeader)], packet, len);
    }

    if (((type & 0xFFFF) == 2) && last_host_peer) {
        enet_peer_send(last_host_peer, Chan_MP, enetpacket);
    } else {
        enet_host_broadcast(enet_host, Chan_MP, enetpacket);
    }

    return len;
}

int MelonLANAdapter::RecvPacketGeneric(u8* packet, bool block, u64* timestamp) {
    if (!enet_host) {
        return 0;
    }

    ProcessLAN(block ? 2 : 1);
    if (rx_queue.empty()) {
        return 0;
    }

    ENetPacket* enetpacket = rx_queue.front();
    rx_queue.pop();
    MelonPacketHeader* header = reinterpret_cast<MelonPacketHeader*>(&enetpacket->data[0]);

    u32 len = header->Length;
    if (len) {
        if (len > 2048) {
            len = 2048;
        }

        std::memcpy(packet, &enetpacket->data[sizeof(MelonPacketHeader)], len);

        if (header->Type == 1) {
            last_host_id = header->SenderID;
            last_host_peer = static_cast<ENetPeer*>(enetpacket->userData);
        }
    }

    if (timestamp) {
        *timestamp = header->Timestamp;
    }
    enet_packet_destroy(enetpacket);
    return len;
}

void MelonLANAdapter::Begin(int inst) {
    if (!enet_host) {
        return;
    }

    connected_bitmask |= (1 << my_player.ID);
    last_host_id = -1;
    last_host_peer = nullptr;

    u8 cmd = Cmd_PlayerConnect;
    ENetPacket* pkt = enet_packet_create(&cmd, 1, ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(enet_host, Chan_Cmd, pkt);
}

void MelonLANAdapter::End(int inst) {
    if (!enet_host) {
        return;
    }

    connected_bitmask &= ~(1 << my_player.ID);

    u8 cmd = Cmd_PlayerDisconnect;
    ENetPacket* pkt = enet_packet_create(&cmd, 1, ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(enet_host, Chan_Cmd, pkt);
}

int MelonLANAdapter::SendPacket(int inst, u8* packet, int len, u64 timestamp) {
    return SendPacketGeneric(0, packet, len, timestamp);
}

int MelonLANAdapter::RecvPacket(int inst, u8* packet, u64* timestamp) {
    return RecvPacketGeneric(packet, false, timestamp);
}

int MelonLANAdapter::SendCmd(int inst, u8* data, int len, u64 timestamp) {
    return SendPacketGeneric(1, data, len, timestamp);
}

int MelonLANAdapter::SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid) {
    return SendPacketGeneric(2 | (aid << 16), data, len, timestamp);
}

int MelonLANAdapter::SendAck(int inst, u8* data, int len, u64 timestamp) {
    return SendPacketGeneric(3, data, len, timestamp);
}

int MelonLANAdapter::RecvHostPacket(int inst, u8* packet, u64* timestamp) {
    if (last_host_id != -1) {
        if (!(connected_bitmask & (1 << last_host_id))) {
            return -1;
        }
    }

    return RecvPacketGeneric(packet, true, timestamp);
}

u16 MelonLANAdapter::RecvReplies(int inst, u8* packets, u64 timestamp, u16 aidmask) {
    if (!enet_host) {
        return 0;
    }

    u16 ret = 0;
    u16 myinstmask = 1 << my_player.ID;

    if ((myinstmask & connected_bitmask) == connected_bitmask) {
        return 0;
    }

    // This is a simplified implementation - full implementation would need
    // semaphore-based waiting like in melonDS
    // For now, just process available packets
    ProcessLAN(0);

    while (!rx_queue.empty()) {
        ENetPacket* enetpacket = rx_queue.front();
        MelonPacketHeader* header = reinterpret_cast<MelonPacketHeader*>(&enetpacket->data[0]);

        if (header->Magic != kMelonPacketMagic) {
            rx_queue.pop();
            enet_packet_destroy(enetpacket);
            continue;
        }

        if ((header->SenderID == my_player.ID) || (header->Timestamp < (timestamp - 32))) {
            rx_queue.pop();
            enet_packet_destroy(enetpacket);
            continue;
        }

        if (header->Length) {
            u32 aid = (header->Type >> 16);
            std::memcpy(&packets[(aid - 1) * 1024], &enetpacket->data[sizeof(MelonPacketHeader)],
                       header->Length);
            ret |= (1 << aid);
        }

        myinstmask |= (1 << header->SenderID);
        if (((myinstmask & connected_bitmask) == connected_bitmask) || ((ret & aidmask) == aidmask)) {
            rx_queue.pop();
            enet_packet_destroy(enetpacket);
            return ret;
        }

        rx_queue.pop();
        enet_packet_destroy(enetpacket);
    }

    return ret;
}

} // namespace Network
