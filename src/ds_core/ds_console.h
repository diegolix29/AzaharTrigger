// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Internal state of one emulated DS/DSi. Compiled together with melonDS only;
// never include this from the frontend (use azalon_ds/azalon_ds.h).

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "azalon_ds/azalon_ds.h"

#include "NDS.h"
#include "types.h"

struct azds_console;

namespace AzalonDS {

/// Buffers writes to a save file and flushes them shortly after the game stops writing,
/// so bursts of small writes don't hammer the disk.
class SaveFile {
public:
    explicit SaveFile(std::string path);
    ~SaveFile();

    /// Called by the core with the complete save buffer.
    void RequestFlush(const melonDS::u8* data, melonDS::u32 length, melonDS::u32 write_offset,
                      melonDS::u32 write_length);
    /// Writes pending data if it has been idle long enough (or always, if force).
    void Flush(bool force);
    const std::string& Path() const {
        return path;
    }

private:
    std::string path;
    std::mutex lock;
    std::vector<melonDS::u8> buffer;
    bool dirty = false;
    std::chrono::steady_clock::time_point last_write{};
};

/// Writes `message` into a C error buffer.
void SetError(char* err, size_t err_len, const std::string& message);

/// Opens a UTF-8 path with fopen semantics on every platform.
FILE* OpenFileUtf8(const std::string& path, const char* mode);

/// Process-wide logger (falls back to stderr if no console registered a callback).
void LogMessage(azds_log_level level, const std::string& message);
void SetGlobalLogCallback(void (*log)(void*, azds_log_level, const char*), void* userdata);

/// Directory for core-managed files (wfcsettings.bin, ...).
void SetDataDir(const std::string& dir);
const std::string& GetDataDir();

/// Networking glue (net.cpp).
void NetRegisterInstance(int instance);
void NetUnregisterInstance(int instance);
int NetSendPacket(melonDS::u8* data, int len, int instance);
int NetRecvPacket(melonDS::u8* data, int instance);

} // namespace AzalonDS

struct azds_console {
    azds_config config{};
    // Owned copies of the strings referenced by config.
    std::string data_dir, ds_bios9, ds_bios7, ds_firmware, dsi_bios9, dsi_bios7, dsi_firmware,
        dsi_nand, firmware_username, firmware_message, dldi_image, dldi_folder, dsi_sd_image,
        dsi_sd_folder;

    std::unique_ptr<melonDS::NDS> nds;
    bool started = false;

    std::unique_ptr<AzalonDS::SaveFile> nds_save;
    std::unique_ptr<AzalonDS::SaveFile> gba_save;
    std::unique_ptr<AzalonDS::SaveFile> firmware_save;

    // RTC offset (seconds) the game set relative to the host clock.
    std::atomic<long long> rtc_offset{0};

    int instance_id = 0;
    bool mic_active = false;
};
