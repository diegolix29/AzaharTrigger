// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <azalon_ds/azalon_ds.h>

#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "citra_qt/ds/ds_config.h"
#include "citra_qt/ds/ds_session.h"
#include "common/logging/log.h"
#include "common/settings.h"

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

void OnCoreStop(void* userdata, int reason) {
    constexpr int kStopReasonExternal = 1; // melonDS StopReason::External: we stopped it
    if (reason == kStopReasonExternal)
        return;
    auto* session = static_cast<Session*>(userdata);
    LOG_INFO(Frontend, "[DS] console stopped by itself (reason {})", reason);
    QMetaObject::invokeMethod(session, "Stopped", Qt::QueuedConnection);
}

QByteArray Utf8(const QString& s) {
    return s.toUtf8();
}

// DS button order in azds key bits -> Azahar NativeButton used for it.
constexpr std::array<std::pair<uint32_t, Settings::NativeButton::Values>, 12> kButtonMap{{
    {AZDS_KEY_A, Settings::NativeButton::A},
    {AZDS_KEY_B, Settings::NativeButton::B},
    {AZDS_KEY_SELECT, Settings::NativeButton::Select},
    {AZDS_KEY_START, Settings::NativeButton::Start},
    {AZDS_KEY_RIGHT, Settings::NativeButton::Right},
    {AZDS_KEY_LEFT, Settings::NativeButton::Left},
    {AZDS_KEY_UP, Settings::NativeButton::Up},
    {AZDS_KEY_DOWN, Settings::NativeButton::Down},
    {AZDS_KEY_R, Settings::NativeButton::R},
    {AZDS_KEY_L, Settings::NativeButton::L},
    {AZDS_KEY_X, Settings::NativeButton::X},
    {AZDS_KEY_Y, Settings::NativeButton::Y},
}};

} // namespace

Session::Session(QObject* parent) : QObject(parent) {
    frame = QImage(AZDS_SCREEN_WIDTH, AZDS_SCREEN_HEIGHT * 2, QImage::Format_RGB32);
    frame.fill(Qt::black);
}

Session::~Session() {
    Stop();
}

bool Session::Start(const QString& path, QString* error) {
    Stop();

    const Config& cfg = Config::Get();
    rom_path = path;

    azds_rom_info info{};
    title = azds_probe_rom(Utf8(path).constData(), &info) && info.title[0]
                ? QString::fromUtf8(info.title).section(QLatin1Char('\n'), 0, 0)
                : QFileInfo(path).completeBaseName();

    // Saves live in <user>/ds/saves/<rom name>.sav. Import a melonDS-style save
    // (<rom>.sav next to the ROM) the first time a game is started.
    const QFileInfo rom_info(path);
    save_path = SavesDir() + rom_info.completeBaseName() + QStringLiteral(".sav");
    const QString side_save = rom_info.absolutePath() + QLatin1Char('/') +
                              rom_info.completeBaseName() + QStringLiteral(".sav");
    if (!QFileInfo::exists(save_path) && QFileInfo::exists(side_save))
        QFile::copy(side_save, save_path);

    // Wireless / internet are process-wide.
    azds_mp_set_mode(static_cast<azds_mp_mode>(cfg.wireless_mode));
    azds_internet_enable(cfg.internet ? 1 : 0);

    OpenAudio();

    const QByteArray data_dir = Utf8(DataDir());
    const QByteArray b9 = Utf8(cfg.ds_bios9), b7 = Utf8(cfg.ds_bios7), fw = Utf8(cfg.ds_firmware);
    const QByteArray ib9 = Utf8(cfg.dsi_bios9), ib7 = Utf8(cfg.dsi_bios7),
                     ifw = Utf8(cfg.dsi_firmware), nand = Utf8(cfg.dsi_nand);
    const QByteArray nickname = Utf8(cfg.nickname);
    const QByteArray dldi_image = Utf8(DataDir() + QStringLiteral("dldi_sd.bin"));
    const QByteArray dldi_folder = Utf8(cfg.dldi_sync_folder);
    const QByteArray dsi_sd_image = Utf8(DataDir() + QStringLiteral("dsi_sd.bin"));

    azds_config config;
    azds_config_init(&config);
    config.console_type = cfg.console_type == 1 ? AZDS_CONSOLE_DSI : AZDS_CONSOLE_DS;
    config.renderer = AZDS_RENDERER_SOFTWARE;
    config.threaded_renderer = cfg.threaded_renderer;
    config.jit_enable = cfg.jit;
    config.direct_boot = cfg.direct_boot;
    config.audio_sample_rate = sink ? sink->GetNativeSampleRate() : 48000.0;
    config.data_dir = data_dir.constData();
    config.ds_bios9_path = b9.constData();
    config.ds_bios7_path = b7.constData();
    config.ds_firmware_path = fw.constData();
    config.dsi_bios9_path = ib9.constData();
    config.dsi_bios7_path = ib7.constData();
    config.dsi_firmware_path = ifw.constData();
    config.dsi_nand_path = nand.constData();
    config.firmware_username = nickname.constData();
    config.firmware_language = cfg.language;
    config.firmware_favorite_color = cfg.favorite_color;
    if (cfg.dldi_enable) {
        config.dldi_image_path = dldi_image.constData();
        config.dldi_image_size = 0; // automatic
        config.dldi_sync_folder = dldi_folder.isEmpty() ? nullptr : dldi_folder.constData();
    }
    config.dsi_sd_image_path = dsi_sd_image.constData();
    config.dsi_sd_image_size = 0;
    config.callbacks.userdata = this;
    config.callbacks.log = LogFromCore;
    config.callbacks.on_stop = OnCoreStop;

    char err[512] = {};
    console = azds_create(&config, err, sizeof(err));
    if (!console) {
        CloseAudio();
        if (error)
            *error = QString::fromUtf8(err);
        return false;
    }

    if (!azds_load_rom(console, Utf8(path).constData(), Utf8(save_path).constData(), err,
                       sizeof(err))) {
        azds_destroy(console);
        console = nullptr;
        CloseAudio();
        if (error)
            *error = QString::fromUtf8(err);
        return false;
    }

    if (!azds_start(console)) {
        azds_destroy(console);
        console = nullptr;
        CloseAudio();
        if (error)
            *error = tr("The DS core could not boot this game.");
        return false;
    }

    LoadInputDevices();

    paused = false;
    running = true;
    emu_thread = std::thread([this] { EmuLoop(); });
    LOG_INFO(Frontend, "[DS] started {} ({})", rom_path.toStdString(), title.toStdString());
    return true;
}

void Session::Stop() {
    if (running) {
        running = false;
        if (emu_thread.joinable())
            emu_thread.join();
    }
    // Fail any commands that never ran.
    {
        std::lock_guard guard(command_lock);
        commands.clear();
    }
    CloseAudio();
    if (console) {
        azds_stop(console);
        azds_destroy(console); // flushes saves
        console = nullptr;
    }
    for (auto& button : buttons)
        button.reset();
    circle_pad.reset();
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

QString Session::StatePath(int slot) const {
    return StatesDir() + QFileInfo(rom_path).completeBaseName() +
           QStringLiteral(".ml%1").arg(slot);
}

bool Session::SaveState(int slot) {
    const QByteArray path = StatePath(slot).toUtf8();
    return RunOnEmuThread([this, path] { return azds_save_state(console, path.constData()) != 0; });
}

bool Session::LoadState(int slot) {
    const QByteArray path = StatePath(slot).toUtf8();
    if (!QFileInfo::exists(StatePath(slot)))
        return false;
    return RunOnEmuThread([this, path] { return azds_load_state(console, path.constData()) != 0; });
}

bool Session::RunOnEmuThread(std::function<bool()> task) {
    if (!console)
        return false;
    if (!running || std::this_thread::get_id() == emu_thread.get_id())
        return task();

    std::packaged_task<bool()> packaged(std::move(task));
    auto result = packaged.get_future();
    {
        std::lock_guard guard(command_lock);
        commands.push_back(std::move(packaged));
    }
    if (result.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        return false;
    try {
        return result.get();
    } catch (const std::future_error&) {
        return false; // the session stopped before running the command
    }
}

QImage Session::CurrentFrame() const {
    std::lock_guard guard(frame_lock);
    return frame.copy();
}

void Session::SetTouch(bool pressed, int x, int y) {
    touch_x = std::clamp(x, 0, AZDS_SCREEN_WIDTH - 1);
    touch_y = std::clamp(y, 0, AZDS_SCREEN_HEIGHT - 1);
    touch_pressed = pressed;
}

void Session::LoadInputDevices() {
    const auto& profile = Settings::values.current_input_profile;
    for (size_t i = 0; i < kButtonMap.size(); i++)
        buttons[i] = Input::CreateDevice<Input::ButtonDevice>(
            profile.buttons[kButtonMap[i].second]);
    circle_pad = Input::CreateDevice<Input::AnalogDevice>(
        profile.analogs[Settings::NativeAnalog::CirclePad]);
}

uint32_t Session::PollButtons() const {
    uint32_t mask = 0;
    for (size_t i = 0; i < kButtonMap.size(); i++)
        if (buttons[i] && buttons[i]->GetStatus())
            mask |= kButtonMap[i].first;

    // The circle pad doubles as the D-pad.
    if (circle_pad) {
        const auto [x, y] = circle_pad->GetStatus();
        constexpr float kThreshold = 0.5f;
        if (x > kThreshold)
            mask |= AZDS_KEY_RIGHT;
        if (x < -kThreshold)
            mask |= AZDS_KEY_LEFT;
        if (y > kThreshold)
            mask |= AZDS_KEY_UP;
        if (y < -kThreshold)
            mask |= AZDS_KEY_DOWN;
    }
    return mask;
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
        // Commands from the GUI (save states, reset...).
        for (;;) {
            std::packaged_task<bool()> task;
            {
                std::lock_guard guard(command_lock);
                if (commands.empty())
                    break;
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

        azds_set_keys(console, PollButtons());
        if (touch_pressed) {
            azds_touch(console, touch_x, touch_y);
            was_touching = true;
        } else if (was_touching) {
            azds_release_touch(console);
            was_touching = false;
        }

        azds_run_frame(console);

        const uint32_t *top = nullptr, *bottom = nullptr;
        if (azds_get_framebuffers(console, &top, &bottom)) {
            std::lock_guard guard(frame_lock);
            constexpr size_t kScreenBytes = AZDS_SCREEN_WIDTH * AZDS_SCREEN_HEIGHT * 4;
            std::memcpy(frame.bits(), top, kScreenBytes);
            std::memcpy(frame.bits() + kScreenBytes, bottom, kScreenBytes);
        }
        emit FrameReady();

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
        if (next_frame < now - std::chrono::milliseconds(100))
            next_frame = now; // we fell far behind: don't try to catch up
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
        if (console && running && !paused)
            done = azds_read_audio(console, buffer, static_cast<int>(num_frames));
        if (static_cast<std::size_t>(done) < num_frames)
            std::memset(buffer + done * 2, 0, (num_frames - done) * 2 * sizeof(s16));
        const float volume = Settings::values.volume.GetValue();
        if (volume < 1.0f) {
            for (std::size_t i = 0; i < num_frames * 2; i++)
                buffer[i] = static_cast<s16>(buffer[i] * volume);
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
