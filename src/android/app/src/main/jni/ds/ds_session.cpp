// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <cstring>

#include <azalon_ds/azalon_ds.h>

#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "jni/ds/ds_session.h"

namespace DS {

namespace {

constexpr double kDSFrameRate = 59.8261; // 33513982 Hz / (6 * 355 * 263)

void LogFromCore(void*, azds_log_level level, const char* message) {
    switch (level) {
    case AZDS_LOG_DEBUG:
        LOG_DEBUG(Frontend, "[DS] {}", message);
        break;
    case AZDS_LOG_INFO:
        LOG_INFO(Frontend, "[DS] {}", message);
        break;
    case AZDS_LOG_WARN:
        LOG_WARNING(Frontend, "[DS] {}", message);
        break;
    default:
        LOG_ERROR(Frontend, "[DS] {}", message);
        break;
    }
}

void OnCoreStop(void*, int reason) {
    constexpr int kStopReasonExternal = 1; // melonDS StopReason::External: we stopped it
    if (reason == kStopReasonExternal) {
        return;
    }
    LOG_INFO(Frontend, "[DS] console stopped by itself (reason {})", reason);
    NotifyStopped();
}

// azalon_ds hands out 0xAARRGGBB words (B,G,R,A in memory); Android's
// RGBA/RGBX_8888 windows want R,G,B,A, so red and blue have to be swapped.
void BlitScreen(const std::uint32_t* src, ANativeWindow_Buffer& buffer) {
    const int width = std::min(buffer.width, AZDS_SCREEN_WIDTH);
    const int height = std::min(buffer.height, AZDS_SCREEN_HEIGHT);
    auto* dst = static_cast<std::uint32_t*>(buffer.bits);

    for (int y = 0; y < height; y++) {
        const std::uint32_t* in = src + static_cast<std::size_t>(y) * AZDS_SCREEN_WIDTH;
        std::uint32_t* out = dst + static_cast<std::size_t>(y) * buffer.stride;
        for (int x = 0; x < width; x++) {
            const std::uint32_t v = in[x];
            out[x] = 0xFF000000u | ((v & 0x00FF0000u) >> 16) | (v & 0x0000FF00u) |
                     ((v & 0x000000FFu) << 16);
        }
    }
}

const char* OrNull(const std::string& s) {
    return s.empty() ? nullptr : s.c_str();
}

} // namespace

Session& Session::Get() {
    static Session session;
    return session;
}

Session::~Session() {
    Stop();
    std::lock_guard guard(surface_lock);
    for (auto*& window : surfaces) {
        if (window) {
            ANativeWindow_release(window);
            window = nullptr;
        }
    }
}

bool Session::Start(const StartParams& params, std::string* error) {
    Stop();

    // Wireless and internet are process-wide in the DS core.
    azds_mp_set_mode(static_cast<azds_mp_mode>(params.wireless_mode));
    azds_internet_enable(params.internet ? 1 : 0);

    OpenAudio();

    azds_config config;
    azds_config_init(&config);
    config.console_type = params.console_type == 1 ? AZDS_CONSOLE_DSI : AZDS_CONSOLE_DS;
    config.renderer = AZDS_RENDERER_SOFTWARE;
    config.threaded_renderer = params.threaded_renderer ? 1 : 0;
    config.jit_enable = params.jit ? 1 : 0;
    config.direct_boot = params.direct_boot ? 1 : 0;
    config.audio_sample_rate = sink ? sink->GetNativeSampleRate() : 48000.0;
    config.data_dir = params.data_dir.c_str();
    config.ds_bios9_path = OrNull(params.ds_bios9);
    config.ds_bios7_path = OrNull(params.ds_bios7);
    config.ds_firmware_path = OrNull(params.ds_firmware);
    config.dsi_bios9_path = OrNull(params.dsi_bios9);
    config.dsi_bios7_path = OrNull(params.dsi_bios7);
    config.dsi_firmware_path = OrNull(params.dsi_firmware);
    config.dsi_nand_path = OrNull(params.dsi_nand);
    config.firmware_username = OrNull(params.nickname);
    config.firmware_language = params.language;
    config.firmware_favorite_color = params.favorite_color;

    const std::string dldi_image = params.data_dir + "dldi_sd.bin";
    if (params.dldi_enable) {
        config.dldi_image_path = dldi_image.c_str();
        config.dldi_image_size = 0; // sized automatically
        config.dldi_sync_folder = OrNull(params.dldi_sync_folder);
    }
    const std::string dsi_sd_image = params.data_dir + "dsi_sd.bin";
    config.dsi_sd_image_path = dsi_sd_image.c_str();
    config.dsi_sd_image_size = 0;

    config.callbacks.log = LogFromCore;
    config.callbacks.on_stop = OnCoreStop;

    char err[512] = {};
    console = azds_create(&config, err, sizeof(err));
    if (!console) {
        CloseAudio();
        if (error) {
            *error = err;
        }
        return false;
    }

    // No ROM = boot the console's own firmware menu (needs BIOS/firmware dumps).
    if (!params.rom_path.empty() &&
        !azds_load_rom(console, params.rom_path.c_str(), params.save_path.c_str(), err,
                       sizeof(err))) {
        azds_destroy(console);
        console = nullptr;
        CloseAudio();
        if (error) {
            *error = err;
        }
        return false;
    }

    if (!azds_start(console)) {
        azds_destroy(console);
        console = nullptr;
        CloseAudio();
        if (error) {
            *error = params.rom_path.empty() ? "The DS core could not boot the firmware."
                                             : "The DS core could not boot this game.";
        }
        return false;
    }

    keys = 0;
    touch_pressed = false;
    paused = false;
    fast_forward = false;
    running = true;
    emu_thread = std::thread([this] { EmuLoop(); });
    LOG_INFO(Frontend, "[DS] started {}",
             params.rom_path.empty() ? std::string("firmware") : params.rom_path);
    return true;
}

void Session::Stop() {
    if (running) {
        running = false;
        if (emu_thread.joinable()) {
            emu_thread.join();
        }
    }
    {
        // Fail any commands that never ran.
        std::lock_guard guard(command_lock);
        commands.clear();
    }
    CloseAudio();
    if (console) {
        azds_stop(console);
        azds_destroy(console); // flushes saves
        console = nullptr;
    }
}

void Session::SetPaused(bool pause) {
    paused = pause;
}

void Session::Reset() {
    RunOnEmuThread([this] {
        azds_reset(console);
        return true;
    });
}

bool Session::SaveState(const std::string& path) {
    return RunOnEmuThread([this, path] { return azds_save_state(console, path.c_str()) != 0; });
}

bool Session::LoadState(const std::string& path) {
    return RunOnEmuThread([this, path] { return azds_load_state(console, path.c_str()) != 0; });
}

bool Session::RunOnEmuThread(std::function<bool()> task) {
    if (!console) {
        return false;
    }
    if (!running || std::this_thread::get_id() == emu_thread.get_id()) {
        return task();
    }

    std::packaged_task<bool()> packaged(std::move(task));
    auto result = packaged.get_future();
    {
        std::lock_guard guard(command_lock);
        commands.push_back(std::move(packaged));
    }
    if (result.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        return false;
    }
    try {
        return result.get();
    } catch (const std::future_error&) {
        return false; // the session stopped before running the command
    }
}

void Session::SetSurface(Screen screen, ANativeWindow* window) {
    const auto index = static_cast<int>(screen);
    if (index < 0 || index >= kNumScreens) {
        if (window) {
            ANativeWindow_release(window);
        }
        return;
    }

    if (window) {
        ANativeWindow_setBuffersGeometry(window, AZDS_SCREEN_WIDTH, AZDS_SCREEN_HEIGHT,
                                         WINDOW_FORMAT_RGBX_8888);
    }

    std::lock_guard guard(surface_lock);
    if (surfaces[index]) {
        ANativeWindow_release(surfaces[index]);
    }
    surfaces[index] = window;
}

void Session::SetTouch(bool pressed, int x, int y) {
    touch_x = std::clamp(x, 0, AZDS_SCREEN_WIDTH - 1);
    touch_y = std::clamp(y, 0, AZDS_SCREEN_HEIGHT - 1);
    touch_pressed = pressed;
}

void Session::SetLidClosed(bool closed) {
    RunOnEmuThread([this, closed] {
        azds_set_lid_closed(console, closed ? 1 : 0);
        return true;
    });
}

void Session::PresentFrame() {
    const std::uint32_t* screens[kNumScreens] = {};
    if (!azds_get_framebuffers(console, &screens[0], &screens[1])) {
        return;
    }

    std::lock_guard guard(surface_lock);
    for (int i = 0; i < kNumScreens; i++) {
        ANativeWindow* window = surfaces[i];
        if (!window || !screens[i]) {
            continue;
        }
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window, &buffer, nullptr) != 0) {
            continue;
        }
        BlitScreen(screens[i], buffer);
        ANativeWindow_unlockAndPost(window);
    }
}

void Session::EmuLoop() {
    using clock = std::chrono::steady_clock;
    const auto frame_time = std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(1.0 / kDSFrameRate));

    auto next_frame = clock::now();
    auto fps_start = clock::now();
    int fps_frames = 0;
    bool was_touching = false;

    while (running) {
        // Commands from the UI thread (reset, save states...).
        for (;;) {
            std::packaged_task<bool()> task;
            {
                std::lock_guard guard(command_lock);
                if (commands.empty()) {
                    break;
                }
                task = std::move(commands.front());
                commands.pop_front();
            }
            task();
        }

        if (paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            next_frame = clock::now();
            continue;
        }

        azds_set_keys(console, keys.load());
        if (touch_pressed) {
            azds_touch(console, touch_x.load(), touch_y.load());
            was_touching = true;
        } else if (was_touching) {
            azds_release_touch(console);
            was_touching = false;
        }

        azds_run_frame(console);
        PresentFrame();

        fps_frames++;
        const auto now = clock::now();
        if (now - fps_start >= std::chrono::seconds(1)) {
            fps = fps_frames / std::chrono::duration<double>(now - fps_start).count();
            fps_frames = 0;
            fps_start = now;
        }

        if (fast_forward) {
            next_frame = clock::now();
            continue;
        }
        next_frame += frame_time;
        if (next_frame < now - std::chrono::milliseconds(100)) {
            next_frame = now; // we fell far behind: don't try to catch up
        }
        std::this_thread::sleep_until(next_frame);
    }
}

void Session::OpenAudio() {
    CloseAudio();
    const auto& details = AudioCore::GetSinkDetails(Settings::values.output_type.GetValue());
    sink = details.create_sink(Settings::values.output_device.GetValue());
    if (!sink) {
        LOG_ERROR(Frontend, "[DS] could not open audio output");
        return;
    }
    sink->SetCallback([this](s16* buffer, std::size_t num_frames) {
        int done = 0;
        if (console && running && !paused) {
            done = azds_read_audio(console, buffer, static_cast<int>(num_frames));
        }
        if (static_cast<std::size_t>(done) < num_frames) {
            std::memset(buffer + static_cast<std::size_t>(done) * 2, 0,
                        (num_frames - static_cast<std::size_t>(done)) * 2 * sizeof(s16));
        }
        const float volume = Settings::values.volume.GetValue();
        if (volume < 1.0f) {
            for (std::size_t i = 0; i < num_frames * 2; i++) {
                buffer[i] = static_cast<s16>(buffer[i] * volume);
            }
        }
    });
}

void Session::CloseAudio() {
    if (sink) {
        sink->SetCallback([](s16* buffer, std::size_t num_frames) {
            std::memset(buffer, 0, num_frames * 2 * sizeof(s16));
        });
        sink.reset();
    }
}

} // namespace DS
