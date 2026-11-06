// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <android/native_window.h>

struct azds_console;

namespace AudioCore {
class Sink;
}

namespace DS {

/// Which of the two DS screens a surface shows.
enum class Screen : int {
    Top = 0,
    Bottom = 1,
};
constexpr int kNumScreens = 2;

/// Everything the frontend has to decide before a console can be created.
/// Mirrors the subset of azds_config the Android UI exposes.
struct StartParams {
    std::string rom_path;  ///< real path, or /proc/self/fd/N for a SAF document; empty = firmware
    std::string save_path; ///< <user>/ds/saves/<name>.sav
    std::string data_dir;  ///< <user>/ds/

    int console_type = 0; ///< 0 = DS, 1 = DSi
    bool direct_boot = true;
    bool jit = true;
    bool threaded_renderer = true;

    std::string nickname;
    int language = 1;
    int favorite_color = 0;

    std::string ds_bios9, ds_bios7, ds_firmware;
    std::string dsi_bios9, dsi_bios7, dsi_firmware, dsi_nand;

    bool dldi_enable = true;
    std::string dldi_sync_folder;

    int wireless_mode = 0; ///< azds_mp_mode: -1 off, 0 local, 1 LAN
    bool internet = true;
};

/// The single running DS/DSi game: owns the azalon_ds console, the emulation
/// thread, audio output and the two Android surfaces the screens are blitted to.
///
/// Android only ever runs one game at a time, so this is a singleton; all public
/// methods are safe to call from the UI thread.
class Session {
public:
    static Session& Get();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// Creates the console, loads the ROM and starts emulating.
    /// Returns false and fills `error` on failure.
    bool Start(const StartParams& params, std::string* error);
    void Stop();

    bool IsRunning() const {
        return running;
    }
    bool IsPaused() const {
        return paused;
    }
    void SetPaused(bool pause);
    void Reset();
    void SetFastForward(bool enabled) {
        fast_forward = enabled;
    }

    bool SaveState(const std::string& path);
    bool LoadState(const std::string& path);

    /// Hands over an ANativeWindow reference (may be nullptr to drop the surface).
    /// The session releases it when it is replaced or the session is destroyed.
    void SetSurface(Screen screen, ANativeWindow* window);

    void SetKeys(std::uint32_t pressed_mask) {
        keys = pressed_mask;
    }
    void SetTouch(bool pressed, int x, int y);
    void SetLidClosed(bool closed);

    double Fps() const {
        return fps;
    }

private:
    Session() = default;
    ~Session();

    void EmuLoop();
    void PresentFrame();
    void OpenAudio();
    void CloseAudio();

    /// Runs `task` on the emulation thread between frames (or inline if it isn't
    /// running) and returns its result. melonDS is not thread-safe.
    bool RunOnEmuThread(std::function<bool()> task);

    azds_console* console = nullptr;
    std::thread emu_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> fast_forward{false};
    std::atomic<double> fps{0.0};

    std::mutex command_lock;
    std::deque<std::packaged_task<bool()>> commands;

    std::mutex surface_lock;
    ANativeWindow* surfaces[kNumScreens] = {};

    std::atomic<std::uint32_t> keys{0};
    std::atomic<bool> touch_pressed{false};
    std::atomic<int> touch_x{0}, touch_y{0};

    std::unique_ptr<AudioCore::Sink> sink;
};

/// Notifies the Java side that the console stopped by itself. Set up from
/// ds_jni.cpp, which owns the cached class and method ids.
void NotifyStopped();

} // namespace DS
