// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// JNI entry points of org.citra.citra_emu.ds.DsLan: the melonDS LAN lobby
// (room discovery, host/join, player list, chat). Same calls as the desktop
// frontend's DS::LanDialog, so Android and desktop players meet in one room.

#include <memory>
#include <string>
#include <vector>

#include <jni.h>

#include <azalon_ds/azalon_ds.h>

#include "jni/android_common/android_common.h"

namespace {

std::string OrEmpty(JNIEnv* env, jstring str) {
    return str ? GetJString(env, str) : std::string();
}

const char* OrNull(const std::string& str) {
    return str.empty() ? nullptr : str.c_str();
}

} // namespace

extern "C" {

/// Switches the core to the LAN backend. Refused while a game runs on another
/// backend: swapping it under a running game would drop its wireless state.
JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_nativeUseLan(JNIEnv*, jobject,
                                                                          jboolean game_running) {
    if (azds_mp_get_mode() == AZDS_MP_LAN) {
        return JNI_TRUE;
    }
    if (game_running) {
        return JNI_FALSE;
    }
    azds_mp_set_mode(AZDS_MP_LAN);
    return azds_mp_get_mode() == AZDS_MP_LAN;
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_isLanMode(JNIEnv*, jobject) {
    return azds_mp_get_mode() == AZDS_MP_LAN;
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsLan_process(JNIEnv*, jobject) {
    azds_mp_process();
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_startDiscovery(JNIEnv*, jobject) {
    return azds_lan_start_discovery() != 0;
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsLan_endDiscovery(JNIEnv*, jobject) {
    azds_lan_end_discovery();
}

/// Rooms as a flat array, ROOM_FIELDS strings per room:
/// address, code, name, game, description, players, max players, password (0/1), in game (0/1).
JNIEXPORT jobjectArray JNICALL Java_org_citra_citra_1emu_ds_DsLan_nativeGetRooms(JNIEnv* env,
                                                                                 jobject) {
    azds_lan_room rooms[32];
    const int count = azds_lan_get_rooms(rooms, 32);
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(count) * 9);
    for (int i = 0; i < count; i++) {
        const auto& r = rooms[i];
        out.emplace_back(r.address);
        out.emplace_back(r.room_code);
        out.emplace_back(r.room_name);
        out.emplace_back(r.game_name);
        out.emplace_back(r.description);
        out.emplace_back(std::to_string(r.num_players));
        out.emplace_back(std::to_string(r.max_players));
        out.emplace_back(r.has_password ? "1" : "0");
        out.emplace_back(r.in_game ? "1" : "0");
    }
    return ToJStringArray(env, out);
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_host(JNIEnv* env, jobject,
                                                                  jstring player_name,
                                                                  jint max_players,
                                                                  jstring room_name,
                                                                  jstring password) {
    const std::string name = OrEmpty(env, player_name);
    const std::string room = OrEmpty(env, room_name);
    const std::string pass = OrEmpty(env, password);
    return azds_lan_host(name.c_str(), max_players, room.c_str(), OrNull(pass)) != 0;
}

/// Blocks for up to ~10 s while the host answers: call it off the UI thread.
JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_join(JNIEnv* env, jobject,
                                                                  jstring player_name,
                                                                  jstring address,
                                                                  jstring password) {
    const std::string name = OrEmpty(env, player_name);
    const std::string host = OrEmpty(env, address);
    const std::string pass = OrEmpty(env, password);
    if (host.empty()) {
        return JNI_FALSE;
    }
    return azds_lan_join(name.c_str(), host.c_str(), OrNull(pass)) != 0;
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsLan_leave(JNIEnv*, jobject) {
    azds_lan_leave();
}

/// Players as a flat array, PLAYER_FIELDS strings per player:
/// id, name, status, address, is local (0/1), ping.
JNIEXPORT jobjectArray JNICALL Java_org_citra_citra_1emu_ds_DsLan_nativeGetPlayers(JNIEnv* env,
                                                                                   jobject) {
    azds_lan_player players[16];
    const int count = azds_lan_get_players(players, 16);
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(count) * 6);
    for (int i = 0; i < count; i++) {
        const auto& p = players[i];
        out.emplace_back(std::to_string(p.id));
        out.emplace_back(p.name);
        out.emplace_back(std::to_string(p.status));
        out.emplace_back(p.address);
        out.emplace_back(p.is_local ? "1" : "0");
        out.emplace_back(std::to_string(p.ping));
    }
    return ToJStringArray(env, out);
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_nativeInSession(JNIEnv*, jobject) {
    return azds_lan_in_session() != 0;
}

JNIEXPORT jint JNICALL Java_org_citra_citra_1emu_ds_DsLan_numPlayers(JNIEnv*, jobject) {
    return azds_lan_num_players();
}

JNIEXPORT jint JNICALL Java_org_citra_citra_1emu_ds_DsLan_maxPlayers(JNIEnv*, jobject) {
    return azds_lan_max_players();
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsLan_sendChat(JNIEnv* env, jobject,
                                                                  jstring message) {
    const std::string text = OrEmpty(env, message);
    if (!text.empty()) {
        azds_lan_send_chat(text.c_str());
    }
}

JNIEXPORT jobjectArray JNICALL Java_org_citra_citra_1emu_ds_DsLan_getChat(JNIEnv* env, jobject) {
    auto lines = std::make_unique<char[][300]>(64);
    const int count = azds_lan_get_chat(lines.get(), 64);
    std::vector<std::string> out;
    out.reserve(count);
    for (int i = 0; i < count; i++) {
        out.emplace_back(lines[i]);
    }
    return ToJStringArray(env, out);
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsLan_kick(JNIEnv*, jobject,
                                                                  jint player_id) {
    return azds_lan_kick(player_id) != 0;
}

} // extern "C"
