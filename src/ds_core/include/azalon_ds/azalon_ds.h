// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Public C API of the AzalonDS DS/DSi core (melonDS wrapped as a shared library).
//
// This header deliberately has no melonDS (or Azahar) dependencies and only uses
// C types, so the library can be built with a different compiler than the
// frontend (e.g. clang for melonDS + MSVC for Azahar on Windows).

#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(AZDS_BUILDING_LIBRARY)
#define AZDS_API __declspec(dllexport)
#else
#define AZDS_API __declspec(dllimport)
#endif
#else
#define AZDS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AZDS_API_VERSION 1

#define AZDS_SCREEN_WIDTH 256
#define AZDS_SCREEN_HEIGHT 192

typedef struct azds_console azds_console;

typedef enum azds_console_type {
    AZDS_CONSOLE_DS = 0,
    AZDS_CONSOLE_DSI = 1,
} azds_console_type;

typedef enum azds_renderer {
    AZDS_RENDERER_SOFTWARE = 0,
    // Reserved, not wired up yet:
    AZDS_RENDERER_OPENGL = 1,
    AZDS_RENDERER_OPENGL_COMPUTE = 2,
} azds_renderer;

typedef enum azds_log_level {
    AZDS_LOG_DEBUG = 0,
    AZDS_LOG_INFO = 1,
    AZDS_LOG_WARN = 2,
    AZDS_LOG_ERROR = 3,
} azds_log_level;

// DS buttons. Pass the pressed buttons (1 = pressed) to azds_set_keys().
enum {
    AZDS_KEY_A = 1u << 0,
    AZDS_KEY_B = 1u << 1,
    AZDS_KEY_SELECT = 1u << 2,
    AZDS_KEY_START = 1u << 3,
    AZDS_KEY_RIGHT = 1u << 4,
    AZDS_KEY_LEFT = 1u << 5,
    AZDS_KEY_UP = 1u << 6,
    AZDS_KEY_DOWN = 1u << 7,
    AZDS_KEY_R = 1u << 8,
    AZDS_KEY_L = 1u << 9,
    AZDS_KEY_X = 1u << 10,
    AZDS_KEY_Y = 1u << 11,
};

// Frontend hooks. All are optional (may be NULL). They are called from the
// thread that runs azds_run_frame() unless stated otherwise.
typedef struct azds_callbacks {
    void* userdata;
    // Log output from the core (may be called from any thread).
    void (*log)(void* userdata, azds_log_level level, const char* message);
    // The emulated console stopped by itself (power off, crash...). reason = melonDS StopReason.
    void (*on_stop)(void* userdata, int reason);
    // Microphone: fill up to max_samples mono s16 samples, return how many were written.
    int (*mic_read)(void* userdata, int16_t* samples, int max_samples);
    // DSi cameras: write a width*height frame (XRGB8888, or YUV422 if yuv != 0).
    void (*camera_capture)(void* userdata, int camera, uint32_t* frame, int width, int height,
                           int yuv);
    // Rumble Pak.
    void (*rumble_start)(void* userdata, uint32_t length_ms);
    void (*rumble_stop)(void* userdata);
} azds_callbacks;

typedef struct azds_config {
    uint32_t struct_size; // set by azds_config_init

    azds_console_type console_type;
    azds_renderer renderer;
    int threaded_renderer; // software renderer: render on a separate thread

    // ARM JIT (x86_64 / arm64 only; ignored elsewhere)
    int jit_enable;
    int jit_max_block_size;
    int jit_literal_optimisations;
    int jit_branch_optimisations;
    int jit_fast_memory;

    // 1 = skip the firmware menu and boot the game directly
    int direct_boot;

    double audio_sample_rate; // host output rate, e.g. 48000
    int audio_interpolation;  // melonDS AudioInterpolation (0 = none)
    int audio_bit_depth;      // melonDS AudioBitDepth (0 = auto)

    // Folder for files the core manages itself (wfcsettings.bin, ...). UTF-8.
    const char* data_dir;

    // Optional dumps (UTF-8 paths). NULL/"" = FreeBIOS + generated firmware (DS mode only).
    const char* ds_bios9_path;
    const char* ds_bios7_path;
    const char* ds_firmware_path;
    // Required for DSi mode.
    const char* dsi_bios9_path;
    const char* dsi_bios7_path;
    const char* dsi_firmware_path;
    const char* dsi_nand_path;
    int dsi_dsp_hle;

    // Firmware user settings. Always applied to generated firmware; applied to
    // dumped firmware only if firmware_override is set. NULL/negative/0 = keep.
    int firmware_override;
    const char* firmware_username; // UTF-8, max 10 characters
    const char* firmware_message;  // UTF-8, max 26 characters
    int firmware_language;         // 0=JP 1=EN 2=FR 3=DE 4=IT 5=ES 6=ZH 7=KO, -1 = keep
    int firmware_favorite_color;   // 0-15, -1 = keep
    int firmware_birth_month;      // 1-12, 0 = keep
    int firmware_birth_day;        // 1-31, 0 = keep

    // Multiplayer instance number (0 = first). Also derives a unique MAC address.
    int instance_id;

    // Virtual SD card for homebrew (DLDI). NULL/"" image path = no SD card.
    // size 0 = sized automatically from the sync folder. The sync folder (optional)
    // is mirrored into the image at boot and written back when the console stops.
    const char* dldi_image_path;
    uint64_t dldi_image_size;
    int dldi_read_only;
    const char* dldi_sync_folder;

    // DSi SD card (DSi mode only), same rules as above.
    const char* dsi_sd_image_path;
    uint64_t dsi_sd_image_size;
    int dsi_sd_read_only;
    const char* dsi_sd_sync_folder;

    azds_callbacks callbacks;
} azds_config;

typedef struct azds_rom_info {
    char game_code[5];       // e.g. "ADAE"
    char header_title[13];   // 12-char header title
    char title[256];         // banner title (UTF-8, English; lines joined by '\n')
    uint32_t icon[32 * 32];  // banner icon, ARGB8888 (0 alpha = transparent)
    int has_icon;
    int is_dsi_enhanced;     // unit code 2
    int is_dsi_exclusive;    // unit code 3
    int is_homebrew;
} azds_rom_info;

// ----------------------------------------------------------------------------
// Library

AZDS_API int azds_api_version(void);
AZDS_API const char* azds_melonds_version(void);

// Read title/code/icon from a .nds file without creating a console. Returns 1 on success.
AZDS_API int azds_probe_rom(const char* path, azds_rom_info* out);

// ----------------------------------------------------------------------------
// Console lifetime

AZDS_API void azds_config_init(azds_config* config);

// Returns NULL on failure (error message written to err if given).
AZDS_API azds_console* azds_create(const azds_config* config, char* err, size_t err_len);
AZDS_API void azds_destroy(azds_console* console);

// Insert a cartridge. save_path is created if it does not exist. Returns 1 on success.
AZDS_API int azds_load_rom(azds_console* console, const char* rom_path, const char* save_path,
                           char* err, size_t err_len);
AZDS_API int azds_load_gba_rom(azds_console* console, const char* rom_path,
                               const char* save_path, char* err, size_t err_len);

// Boot (direct boot or firmware, according to the config). Returns 1 on success.
AZDS_API int azds_start(azds_console* console);
AZDS_API void azds_reset(azds_console* console);
AZDS_API void azds_stop(azds_console* console);
AZDS_API int azds_is_running(const azds_console* console);

// Emulate one video frame. Returns the number of scanlines run.
AZDS_API uint32_t azds_run_frame(azds_console* console);

// Software renderer output: 256x192 per screen, 32-bit BGRA (little-endian 0xAARRGGBB).
// Returns 1 if RAM framebuffers are available.
AZDS_API int azds_get_framebuffers(azds_console* console, const uint32_t** top,
                                   const uint32_t** bottom);

// ----------------------------------------------------------------------------
// Input

AZDS_API void azds_set_keys(azds_console* console, uint32_t pressed_mask);
AZDS_API void azds_touch(azds_console* console, int x, int y);
AZDS_API void azds_release_touch(azds_console* console);
AZDS_API void azds_set_lid_closed(azds_console* console, int closed);

// ----------------------------------------------------------------------------
// Audio: interleaved stereo s16 at config.audio_sample_rate.

AZDS_API int azds_read_audio(azds_console* console, int16_t* stereo_out, int max_frames);
AZDS_API int azds_audio_frames_available(azds_console* console);
AZDS_API void azds_set_audio_sample_rate(azds_console* console, double rate);

// ----------------------------------------------------------------------------
// Saves / state

AZDS_API void azds_flush_saves(azds_console* console);
AZDS_API int azds_save_state(azds_console* console, const char* path);
AZDS_API int azds_load_state(azds_console* console, const char* path);

// Action Replay cheats. codes: whitespace-separated hex words ("XXXXXXXX YYYYYYYY ...").
AZDS_API void azds_clear_cheats(azds_console* console);
AZDS_API int azds_add_cheat(azds_console* console, const char* name, const char* codes,
                            int enabled);

// ----------------------------------------------------------------------------
// Wireless (local multiplayer) and internet (Nintendo WFC via user-mode NAT).
// These are process-wide and shared by all consoles.

typedef enum azds_mp_mode {
    AZDS_MP_NONE = -1, // wireless disabled
    AZDS_MP_LOCAL = 0, // several consoles in this process
    AZDS_MP_LAN = 1,   // consoles on different machines (LAN / VPN)
} azds_mp_mode;

AZDS_API void azds_mp_set_mode(azds_mp_mode mode);
AZDS_API azds_mp_mode azds_mp_get_mode(void);

typedef struct azds_lan_room {
    char address[64]; // dotted IPv4 of the host, pass to azds_lan_join()
    char room_code[9];
    char room_name[64];
    char game_name[64];
    char description[128]; // AzalonDS hosts put their player name here
    int num_players;
    int max_players;
    int has_password;
    int in_game;
} azds_lan_room;

typedef struct azds_lan_player {
    int id;
    char name[32];
    int status; // 0 none, 1 client, 2 host, 3 connecting, 4 disconnected
    char address[64];
    int is_local;
    uint32_t ping;
} azds_lan_player;

// All LAN functions require azds_mp_set_mode(AZDS_MP_LAN) first.
AZDS_API int azds_lan_start_discovery(void);
AZDS_API void azds_lan_end_discovery(void);
AZDS_API int azds_lan_get_rooms(azds_lan_room* out, int max_rooms);
AZDS_API int azds_lan_host(const char* player_name, int max_players, const char* room_name,
                           const char* password);
AZDS_API int azds_lan_join(const char* player_name, const char* host_address,
                           const char* password);
AZDS_API void azds_lan_leave(void);
AZDS_API int azds_lan_get_players(azds_lan_player* out, int max_players);
// 1 while hosting or joined to a room (browsing for rooms does not count).
AZDS_API int azds_lan_in_session(void);
AZDS_API int azds_lan_num_players(void);
AZDS_API int azds_lan_max_players(void);
AZDS_API void azds_lan_send_chat(const char* message);
// Copies up to max_lines chat lines (oldest first) formatted as "<sender_id>: text".
AZDS_API int azds_lan_get_chat(char (*lines)[300], int max_lines);
// Structured chat: sender name is captured when the message arrives, so it survives
// the sender leaving. azds_lan_chat_total() counts every message of the session (it
// keeps growing after the core drops old lines), so frontends can spot new ones.
typedef struct azds_lan_chat {
    int sender_id;
    char sender[32];
    int is_local;
    char text[256];
} azds_lan_chat;
AZDS_API int azds_lan_get_chat_messages(azds_lan_chat* out, int max_messages);
AZDS_API unsigned azds_lan_chat_total(void);
// 1 while hosting a room (not when joined to someone else's).
AZDS_API int azds_lan_is_host(void);
// The room this console is hosting or joined to. address is the host's IPv4 for
// clients and empty for the host. Returns 0 when not in a room.
AZDS_API int azds_lan_get_current_room(azds_lan_room* out);
// Host only: the game the room plays (shown in room lists) and whether it is running.
AZDS_API void azds_lan_set_game(const char* title, int in_game);
// The IPv4 address this device uses on its network (what others type to join by IP).
// Returns 0 when there is no network. Works without LAN mode.
AZDS_API int azds_lan_local_address(char* out, int out_len);
AZDS_API int azds_lan_kick(int player_id);
AZDS_API int azds_lan_ban(int player_id);
// Pumps the wireless backend. azds_run_frame() already calls this once per frame;
// call it yourself only while no console is running (e.g. in a lobby dialog).
AZDS_API void azds_mp_process(void);

// Internet: melonDS user-mode networking (libslirp), no admin rights needed.
// Point the game's Nintendo WFC DNS at a replacement service (Wiimmfi, AltWFC...).
AZDS_API int azds_internet_enable(int enable);
AZDS_API int azds_internet_is_enabled(void);

#ifdef __cplusplus
}
#endif
