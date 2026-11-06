// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Args.h"
#include "DSi.h"
#include "DSi_NAND.h"
#include "FreeBIOS.h"
#include "GBACart.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "NDS.h"
#include "NDSCart.h"
#include "Platform.h"
#include "SPI_Firmware.h"
#include "Savestate.h"
#include "net/MPInterface.h"
#include "version.h"

#include "ds_console.h"

using namespace melonDS;
using melonDS::Platform::LogLevel;

namespace fs = std::filesystem;

// ============================================================================
// Shared helpers

namespace AzalonDS {

namespace {
std::mutex g_log_lock;
void (*g_log_callback)(void*, azds_log_level, const char*) = nullptr;
void* g_log_userdata = nullptr;
std::string g_data_dir;
} // namespace

void SetGlobalLogCallback(void (*log)(void*, azds_log_level, const char*), void* userdata) {
    std::lock_guard guard(g_log_lock);
    g_log_callback = log;
    g_log_userdata = userdata;
}

void LogMessage(azds_log_level level, const std::string& message) {
    std::lock_guard guard(g_log_lock);
    if (g_log_callback) {
        g_log_callback(g_log_userdata, level, message.c_str());
        return;
    }
    static const char* const kNames[] = {"debug", "info", "warn", "error"};
    const int idx = std::clamp(static_cast<int>(level), 0, 3);
    std::fprintf(stderr, "[AzalonDS/%s] %s\n", kNames[idx], message.c_str());
}

void SetDataDir(const std::string& dir) {
    g_data_dir = dir;
}

const std::string& GetDataDir() {
    return g_data_dir;
}

void SetError(char* err, size_t err_len, const std::string& message) {
    LogMessage(AZDS_LOG_ERROR, message);
    if (err && err_len > 0)
        std::snprintf(err, err_len, "%s", message.c_str());
}

SaveFile::SaveFile(std::string path_) : path(std::move(path_)) {}

SaveFile::~SaveFile() {
    Flush(true);
}

void SaveFile::RequestFlush(const u8* data, u32 length, u32 /*write_offset*/,
                            u32 /*write_length*/) {
    std::lock_guard guard(lock);
    buffer.assign(data, data + length);
    dirty = true;
    last_write = std::chrono::steady_clock::now();
}

void SaveFile::Flush(bool force) {
    std::lock_guard guard(lock);
    if (!dirty || path.empty())
        return;
    if (!force && std::chrono::steady_clock::now() - last_write < std::chrono::seconds(1))
        return;

    FILE* f = OpenFileUtf8(path, "wb");
    if (!f) {
        LogMessage(AZDS_LOG_ERROR, "Could not write save file " + path);
        return;
    }
    std::fwrite(buffer.data(), 1, buffer.size(), f);
    std::fclose(f);
    dirty = false;
}

} // namespace AzalonDS

namespace {

using AzalonDS::LogMessage;
using AzalonDS::SetError;

std::string Str(const char* s) {
    return s ? std::string(s) : std::string();
}

bool ReadWholeFile(const std::string& path, std::vector<u8>& out) {
    FILE* f = AzalonDS::OpenFileUtf8(path, "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(len));
    const size_t read = len ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return read == out.size();
}

std::u16string Utf8ToUtf16(const std::string& in) {
    std::u16string out;
    size_t i = 0;
    while (i < in.size()) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        char32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07;
            extra = 3;
        } else {
            i++;
            continue;
        }
        if (i + extra >= in.size())
            break; // truncated sequence
        for (int k = 1; k <= extra; k++)
            cp = (cp << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
        i += 1 + extra;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string Utf16ToUtf8(const char16_t* in, size_t max_len) {
    std::string out;
    for (size_t i = 0; i < max_len && in[i]; i++) {
        char32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < max_len && in[i + 1] >= 0xDC00 &&
            in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            i++;
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

template <typename Image>
std::unique_ptr<Image> LoadBios(const std::string& path, const char* what) {
    std::vector<u8> data;
    if (!ReadWholeFile(path, data) || data.size() < sizeof(Image)) {
        LogMessage(AZDS_LOG_ERROR, std::string(what) + " not found or too small: " + path);
        return nullptr;
    }
    auto bios = std::make_unique<Image>();
    std::memcpy(bios->data(), data.data(), bios->size());
    return bios;
}

std::string WifiSettingsFile(int instance) {
    return instance > 0 ? "wfcsettings" + std::to_string(instance + 1) + ".bin"
                        : std::string("wfcsettings.bin");
}

void CustomizeFirmware(const azds_console& console, Firmware& firmware, bool override_settings) {
    const azds_config& cfg = console.config;
    if (override_settings) {
        auto& header = firmware.GetHeader();
        auto& data = firmware.GetEffectiveUserData();

        if (!console.firmware_username.empty()) {
            const auto name = Utf8ToUtf16(console.firmware_username);
            const size_t len = std::min<size_t>(name.size(), 10);
            data.NameLength = static_cast<u8>(len);
            std::memcpy(data.Nickname, name.data(), len * sizeof(char16_t));
        }

        if (cfg.firmware_language >= 0 && cfg.firmware_language <= 7) {
            const auto language = static_cast<Firmware::Language>(cfg.firmware_language);
            const bool extlang = language >= Firmware::Language::Chinese;
            data.Settings &= ~Firmware::Language::Reserved;
            data.Settings |= extlang ? Firmware::Language::English : language;
            data.ExtendedSettings.ExtendedLanguage = language;
            if (extlang && !(header.ConsoleType & 0x40)) {
                if (header.ConsoleType == 0xFF)
                    header.ConsoleType = 0x43;
                else
                    header.ConsoleType |= 0x43;
                data.ExtendedSettings.Unknown0 = 0x01;
                data.ExtendedSettings.SupportedLanguageMask = 0x7F;
            }
        }

        if (cfg.firmware_favorite_color >= 0 && cfg.firmware_favorite_color <= 15)
            data.FavoriteColor = static_cast<u8>(cfg.firmware_favorite_color);
        if (cfg.firmware_birth_month >= 1 && cfg.firmware_birth_month <= 12)
            data.BirthdayMonth = static_cast<u8>(cfg.firmware_birth_month);
        if (cfg.firmware_birth_day >= 1 && cfg.firmware_birth_day <= 31)
            data.BirthdayDay = static_cast<u8>(cfg.firmware_birth_day);

        if (!console.firmware_message.empty()) {
            const auto msg = Utf8ToUtf16(console.firmware_message);
            const size_t len = std::min<size_t>(msg.size(), 26);
            data.MessageLength = static_cast<u8>(len);
            std::memcpy(data.Message, msg.data(), len * sizeof(char16_t));
        }
    }

    // Every multiplayer instance needs its own MAC address.
    if (console.instance_id > 0) {
        auto& header = firmware.GetHeader();
        MacAddress mac = header.MacAddr;
        const int id = console.instance_id;
        mac[3] += id;
        mac[4] += id * 0x44;
        mac[5] += id * 0x10;
        mac[0] &= 0xFC; // not a broadcast/multicast address
        header.MacAddr = mac;
        header.UpdateChecksum();
    }

    firmware.UpdateChecksums();
}

std::optional<Firmware> LoadOrGenerateFirmware(azds_console& console, int type,
                                               const std::string& path) {
    if (path.empty()) {
        // Generated firmware; keep its Wi-Fi/WFC settings in the data folder.
        Firmware firmware(type);
        const std::string settings_path =
            Platform::GetLocalFilePath(WifiSettingsFile(console.instance_id));
        std::vector<u8> settings;
        constexpr size_t kSettingsSize = 3 * (sizeof(Firmware::WifiAccessPoint) +
                                              sizeof(Firmware::ExtendedWifiAccessPoint));
        if (ReadWholeFile(settings_path, settings) && settings.size() >= kSettingsSize) {
            std::memcpy(firmware.GetExtendedAccessPointPosition(), settings.data(),
                        kSettingsSize);
            firmware.UpdateChecksums();
        }
        CustomizeFirmware(console, firmware, true);
        console.firmware_save = std::make_unique<AzalonDS::SaveFile>(settings_path);
        return firmware;
    }

    Platform::FileHandle* file = Platform::OpenFile(path, Platform::FileMode::Read);
    if (!file) {
        LogMessage(AZDS_LOG_ERROR, "Couldn't open firmware " + path);
        return std::nullopt;
    }
    Firmware firmware(file);
    Platform::CloseFile(file);
    if (!firmware.Buffer()) {
        LogMessage(AZDS_LOG_ERROR, "Couldn't read firmware " + path);
        return std::nullopt;
    }
    CustomizeFirmware(console, firmware, console.config.firmware_override != 0);
    console.firmware_save = std::make_unique<AzalonDS::SaveFile>(path);
    return firmware;
}

std::optional<FATStorageArgs> SDCardArgs(const std::string& image, u64 size, bool read_only,
                                         const std::string& folder) {
    if (image.empty())
        return std::nullopt;
    FATStorageArgs args{};
    args.Filename = image;
    args.Size = size;
    args.ReadOnly = read_only;
    if (!folder.empty())
        args.SourceDir = folder;
    return args;
}

void ApplyRendererSettings(azds_console& console) {
    console.nds->SetRenderer(std::make_unique<SoftRenderer>(*console.nds));
    RendererSettings settings{};
    settings.ScaleFactor = 1;
    settings.Threaded = console.config.threaded_renderer != 0;
    settings.HiresCoordinates = false;
    settings.BetterPolygons = false;
    console.nds->GetRenderer().SetRenderSettings(settings);
}

void SetDateTime(azds_console& console) {
    const std::time_t now = std::time(nullptr) + static_cast<std::time_t>(console.rtc_offset);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    console.nds->RTC.SetDateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                 local.tm_hour, local.tm_min, local.tm_sec);
}

void SetupBoot(azds_console& console) {
    NDS& nds = *console.nds;
    if (nds.GetNDSCart() && (console.config.direct_boot || nds.NeedsDirectBoot())) {
        std::string unused;
        nds.SetupDirectBoot(unused);
    }
}

} // namespace

// ============================================================================
// C API

extern "C" {

AZDS_API int azds_api_version(void) {
    return AZDS_API_VERSION;
}

AZDS_API const char* azds_melonds_version(void) {
    return MELONDS_VERSION;
}

AZDS_API void azds_config_init(azds_config* config) {
    if (!config)
        return;
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->console_type = AZDS_CONSOLE_DS;
    config->renderer = AZDS_RENDERER_SOFTWARE;
    config->threaded_renderer = 1;
    config->jit_enable = 1;
    config->jit_max_block_size = 32;
    config->jit_literal_optimisations = 1;
    config->jit_branch_optimisations = 1;
    config->jit_fast_memory = 1;
    config->direct_boot = 1;
    config->audio_sample_rate = 48000.0;
    config->firmware_language = -1;
    config->firmware_favorite_color = -1;
}

AZDS_API azds_console* azds_create(const azds_config* config, char* err, size_t err_len) {
    if (!config || config->struct_size != sizeof(azds_config)) {
        SetError(err, err_len, "azds_create: invalid or incompatible config");
        return nullptr;
    }

    auto console = std::make_unique<azds_console>();
    console->config = *config;
    console->data_dir = Str(config->data_dir);
    console->ds_bios9 = Str(config->ds_bios9_path);
    console->ds_bios7 = Str(config->ds_bios7_path);
    console->ds_firmware = Str(config->ds_firmware_path);
    console->dsi_bios9 = Str(config->dsi_bios9_path);
    console->dsi_bios7 = Str(config->dsi_bios7_path);
    console->dsi_firmware = Str(config->dsi_firmware_path);
    console->dsi_nand = Str(config->dsi_nand_path);
    console->firmware_username = Str(config->firmware_username);
    console->firmware_message = Str(config->firmware_message);
    console->dldi_image = Str(config->dldi_image_path);
    console->dldi_folder = Str(config->dldi_sync_folder);
    console->dsi_sd_image = Str(config->dsi_sd_image_path);
    console->dsi_sd_folder = Str(config->dsi_sd_sync_folder);
    console->instance_id = std::max(0, config->instance_id);

    // Don't keep pointers to caller memory.
    console->config.data_dir = nullptr;
    console->config.ds_bios9_path = console->config.ds_bios7_path = nullptr;
    console->config.ds_firmware_path = nullptr;
    console->config.dsi_bios9_path = console->config.dsi_bios7_path = nullptr;
    console->config.dsi_firmware_path = console->config.dsi_nand_path = nullptr;
    console->config.firmware_username = console->config.firmware_message = nullptr;
    console->config.dldi_image_path = console->config.dldi_sync_folder = nullptr;
    console->config.dsi_sd_image_path = console->config.dsi_sd_sync_folder = nullptr;

    if (config->callbacks.log)
        AzalonDS::SetGlobalLogCallback(config->callbacks.log, config->callbacks.userdata);
    if (!console->data_dir.empty()) {
        std::error_code ec;
        fs::create_directories(fs::u8path(console->data_dir), ec);
        AzalonDS::SetDataDir(console->data_dir);
    }

    const bool dsi = config->console_type == AZDS_CONSOLE_DSI;

    // BIOS
    std::unique_ptr<ARM9BIOSImage> arm9bios;
    std::unique_ptr<ARM7BIOSImage> arm7bios;
    // DSi mode still needs the DS-mode BIOS (FreeBIOS is not DSi compatible).
    const std::string& bios9_path = console->ds_bios9;
    const std::string& bios7_path = console->ds_bios7;
    if (!bios9_path.empty() && !bios7_path.empty()) {
        arm9bios = LoadBios<ARM9BIOSImage>(bios9_path, "DS ARM9 BIOS");
        arm7bios = LoadBios<ARM7BIOSImage>(bios7_path, "DS ARM7 BIOS");
        if (!arm9bios || !arm7bios) {
            SetError(err, err_len, "Could not load the DS BIOS files");
            return nullptr;
        }
    } else {
        arm9bios = std::make_unique<ARM9BIOSImage>(FreeBIOSGetNtrArm9());
        arm7bios = std::make_unique<ARM7BIOSImage>(FreeBIOSGetNtrArm7());
    }

    // Firmware
    auto firmware = LoadOrGenerateFirmware(*console, dsi ? 1 : 0,
                                           dsi ? console->dsi_firmware : console->ds_firmware);
    if (!firmware) {
        SetError(err, err_len, "Could not load the firmware");
        return nullptr;
    }

    std::optional<JITArgs> jit = std::nullopt;
#ifdef JIT_ENABLED
    if (config->jit_enable) {
        JITArgs args{};
        args.MaxBlockSize = static_cast<unsigned>(std::clamp(config->jit_max_block_size, 1, 32));
        args.LiteralOptimizations = config->jit_literal_optimisations != 0;
        args.BranchOptimizations = config->jit_branch_optimisations != 0;
        args.FastMemory = config->jit_fast_memory != 0;
        jit = args;
    }
#endif

    NDSArgs ndsargs{};
    ndsargs.ARM9BIOS = std::move(arm9bios);
    ndsargs.ARM7BIOS = std::move(arm7bios);
    ndsargs.Firmware = std::move(*firmware);
    ndsargs.JIT = jit;
    ndsargs.BitDepth = static_cast<AudioBitDepth>(config->audio_bit_depth);
    ndsargs.Interpolation = static_cast<AudioInterpolation>(config->audio_interpolation);
    ndsargs.OutputSampleRate =
        config->audio_sample_rate > 0 ? config->audio_sample_rate : 48000.0;

    AzalonDS::NetRegisterInstance(console->instance_id);

    if (dsi) {
        auto arm9i = LoadBios<DSiBIOSImage>(console->dsi_bios9, "DSi ARM9 BIOS");
        auto arm7i = LoadBios<DSiBIOSImage>(console->dsi_bios7, "DSi ARM7 BIOS");
        if (!arm9i || !arm7i) {
            AzalonDS::NetUnregisterInstance(console->instance_id);
            SetError(err, err_len, "DSi mode needs the DSi ARM9 and ARM7 BIOS dumps");
            return nullptr;
        }

        Platform::FileHandle* nand_file =
            Platform::OpenFile(console->dsi_nand, Platform::FileMode::ReadWriteExisting);
        if (!nand_file) {
            AzalonDS::NetUnregisterInstance(console->instance_id);
            SetError(err, err_len, "DSi mode needs a DSi NAND dump: " + console->dsi_nand);
            return nullptr;
        }
        DSi_NAND::NANDImage nand(nand_file, &(*arm7i)[0x8308]);
        if (!nand) {
            AzalonDS::NetUnregisterInstance(console->instance_id);
            SetError(err, err_len, "Failed to parse the DSi NAND dump");
            return nullptr;
        }

        std::optional<FATStorage> sd_card = std::nullopt;
        if (auto sd_args = SDCardArgs(console->dsi_sd_image, config->dsi_sd_image_size,
                                      config->dsi_sd_read_only != 0, console->dsi_sd_folder))
            sd_card.emplace(*sd_args);

        DSiArgs dsiargs{std::move(ndsargs), std::move(arm9i), std::move(arm7i), std::move(nand),
                        std::move(sd_card), config->dsi_dsp_hle != 0};
        console->nds = std::make_unique<DSi>(std::move(dsiargs), console.get());
    } else {
        console->nds = std::make_unique<NDS>(std::move(ndsargs), console.get());
    }

    ApplyRendererSettings(*console);
    console->nds->Reset();
    SetDateTime(*console);
    return console.release();
}

AZDS_API void azds_destroy(azds_console* console) {
    if (!console)
        return;
    if (console->nds && console->started)
        console->nds->Stop();
    azds_flush_saves(console);
    console->nds.reset();
    AzalonDS::NetUnregisterInstance(console->instance_id);
    delete console;
}

AZDS_API int azds_load_rom(azds_console* console, const char* rom_path, const char* save_path,
                           char* err, size_t err_len) {
    if (!console || !rom_path) {
        SetError(err, err_len, "azds_load_rom: bad arguments");
        return 0;
    }

    std::vector<u8> rom;
    if (!ReadWholeFile(rom_path, rom) || rom.empty() || rom.size() > 0x40000000) {
        SetError(err, err_len, std::string("Could not read ROM ") + rom_path);
        return 0;
    }

    std::unique_ptr<u8[]> sram;
    u32 sram_len = 0;
    const std::string save = Str(save_path);
    if (!save.empty()) {
        std::vector<u8> sram_data;
        if (ReadWholeFile(save, sram_data) && !sram_data.empty()) {
            sram_len = static_cast<u32>(sram_data.size());
            sram = std::make_unique<u8[]>(sram_len);
            std::memcpy(sram.get(), sram_data.data(), sram_len);
        }
    }

    auto rom_data = std::make_unique<u8[]>(rom.size());
    std::memcpy(rom_data.get(), rom.data(), rom.size());

    NDSCart::NDSCartArgs cartargs{};
    // Only used if the ROM turns out to be homebrew (DLDI).
    cartargs.SDCard = SDCardArgs(console->dldi_image, console->config.dldi_image_size,
                                 console->config.dldi_read_only != 0, console->dldi_folder);
    cartargs.SRAM = std::move(sram);
    cartargs.SRAMLength = sram_len;

    auto cart = NDSCart::ParseROM(std::move(rom_data), static_cast<u32>(rom.size()), console,
                                  std::move(cartargs));
    if (!cart) {
        SetError(err, err_len, std::string("Not a valid DS ROM: ") + rom_path);
        return 0;
    }

    console->nds->SetNDSCart(std::move(cart));
    console->nds_save = std::make_unique<AzalonDS::SaveFile>(save);
    return 1;
}

AZDS_API int azds_load_gba_rom(azds_console* console, const char* rom_path,
                               const char* save_path, char* err, size_t err_len) {
    if (!console || !rom_path) {
        SetError(err, err_len, "azds_load_gba_rom: bad arguments");
        return 0;
    }
    if (console->nds->ConsoleType != 0) {
        SetError(err, err_len, "The GBA slot is only available in DS mode");
        return 0;
    }

    std::vector<u8> rom;
    if (!ReadWholeFile(rom_path, rom) || rom.empty()) {
        SetError(err, err_len, std::string("Could not read GBA ROM ") + rom_path);
        return 0;
    }
    auto rom_data = std::make_unique<u8[]>(rom.size());
    std::memcpy(rom_data.get(), rom.data(), rom.size());

    std::unique_ptr<u8[]> sram;
    u32 sram_len = 0;
    const std::string save = Str(save_path);
    std::vector<u8> sram_data;
    if (!save.empty() && ReadWholeFile(save, sram_data) && !sram_data.empty()) {
        sram_len = static_cast<u32>(sram_data.size());
        sram = std::make_unique<u8[]>(sram_len);
        std::memcpy(sram.get(), sram_data.data(), sram_len);
    }

    auto cart = GBACart::ParseROM(std::move(rom_data), static_cast<u32>(rom.size()),
                                  std::move(sram), sram_len, console);
    if (!cart) {
        SetError(err, err_len, std::string("Not a valid GBA ROM: ") + rom_path);
        return 0;
    }
    console->nds->SetGBACart(std::move(cart));
    console->gba_save = std::make_unique<AzalonDS::SaveFile>(save);
    return 1;
}

AZDS_API int azds_start(azds_console* console) {
    if (!console || !console->nds)
        return 0;
    SetupBoot(*console);
    console->nds->Start();
    console->started = true;
    return 1;
}

AZDS_API void azds_reset(azds_console* console) {
    if (!console || !console->nds)
        return;
    console->nds->Reset();
    SetDateTime(*console);
    SetupBoot(*console);
    console->nds->Start();
    console->started = true;
}

AZDS_API void azds_stop(azds_console* console) {
    if (!console || !console->nds || !console->started)
        return;
    console->nds->Stop();
    console->started = false;
    azds_flush_saves(console);
}

AZDS_API int azds_is_running(const azds_console* console) {
    return console && console->nds && console->started && console->nds->IsRunning() ? 1 : 0;
}

AZDS_API uint32_t azds_run_frame(azds_console* console) {
    if (!console || !console->nds || !console->started)
        return 0;
    MPInterface::Get().Process();
    const u32 lines = console->nds->RunFrame();
    if (console->nds_save)
        console->nds_save->Flush(false);
    if (console->gba_save)
        console->gba_save->Flush(false);
    if (console->firmware_save)
        console->firmware_save->Flush(false);
    return lines;
}

AZDS_API int azds_get_framebuffers(azds_console* console, const uint32_t** top,
                                   const uint32_t** bottom) {
    if (!console || !console->nds)
        return 0;
    void* t = nullptr;
    void* b = nullptr;
    if (!console->nds->GPU.GetFramebuffers(&t, &b))
        return 0;
    if (top)
        *top = static_cast<const uint32_t*>(t);
    if (bottom)
        *bottom = static_cast<const uint32_t*>(b);
    return t && b ? 1 : 0;
}

AZDS_API void azds_set_keys(azds_console* console, uint32_t pressed_mask) {
    if (console && console->nds)
        console->nds->SetKeyMask(~pressed_mask & 0xFFF); // melonDS: 0 = pressed
}

AZDS_API void azds_touch(azds_console* console, int x, int y) {
    if (!console || !console->nds)
        return;
    console->nds->TouchScreen(static_cast<u16>(std::clamp(x, 0, AZDS_SCREEN_WIDTH - 1)),
                              static_cast<u16>(std::clamp(y, 0, AZDS_SCREEN_HEIGHT - 1)));
}

AZDS_API void azds_release_touch(azds_console* console) {
    if (console && console->nds)
        console->nds->ReleaseScreen();
}

AZDS_API void azds_set_lid_closed(azds_console* console, int closed) {
    if (console && console->nds)
        console->nds->SetLidClosed(closed != 0);
}

AZDS_API int azds_read_audio(azds_console* console, int16_t* stereo_out, int max_frames) {
    if (!console || !console->nds || !stereo_out || max_frames <= 0)
        return 0;
    return console->nds->SPU.ReadOutput(stereo_out, max_frames);
}

AZDS_API int azds_audio_frames_available(azds_console* console) {
    if (!console || !console->nds)
        return 0;
    return console->nds->SPU.GetOutputSize();
}

AZDS_API void azds_set_audio_sample_rate(azds_console* console, double rate) {
    if (console && console->nds && rate > 0)
        console->nds->SPU.SetOutputSampleRate(rate);
}

AZDS_API void azds_flush_saves(azds_console* console) {
    if (!console)
        return;
    if (console->nds_save)
        console->nds_save->Flush(true);
    if (console->gba_save)
        console->gba_save->Flush(true);
    if (console->firmware_save)
        console->firmware_save->Flush(true);
}

AZDS_API int azds_save_state(azds_console* console, const char* path) {
    if (!console || !console->nds || !path)
        return 0;
    Savestate state;
    if (state.Error || !console->nds->DoSavestate(&state) || state.Error)
        return 0;
    state.Finish();
    FILE* f = AzalonDS::OpenFileUtf8(path, "wb");
    if (!f)
        return 0;
    const size_t written = std::fwrite(state.Buffer(), 1, state.Length(), f);
    std::fclose(f);
    return written == state.Length() ? 1 : 0;
}

AZDS_API int azds_load_state(azds_console* console, const char* path) {
    if (!console || !console->nds || !path)
        return 0;
    std::vector<u8> data;
    if (!ReadWholeFile(path, data) || data.empty())
        return 0;
    Savestate state(data.data(), static_cast<u32>(data.size()), false);
    if (state.Error)
        return 0;
    if (!console->nds->DoSavestate(&state) || state.Error) {
        LogMessage(AZDS_LOG_ERROR, std::string("Failed to load state ") + path);
        return 0;
    }
    return 1;
}

AZDS_API void azds_clear_cheats(azds_console* console) {
    if (console && console->nds)
        console->nds->AREngine.Cheats.clear();
}

AZDS_API int azds_add_cheat(azds_console* console, const char* name, const char* codes,
                            int enabled) {
    if (!console || !console->nds || !codes)
        return 0;
    ARCode code{};
    code.Parent = nullptr;
    code.Name = Str(name);
    code.Enabled = enabled != 0;
    std::istringstream stream(codes);
    std::string word;
    while (stream >> word) {
        try {
            code.Code.push_back(static_cast<u32>(std::stoul(word, nullptr, 16)));
        } catch (...) {
            return 0;
        }
    }
    if (code.Code.empty() || (code.Code.size() & 1))
        return 0;
    console->nds->AREngine.Cheats.push_back(std::move(code));
    return 1;
}

AZDS_API int azds_probe_rom(const char* path, azds_rom_info* out) {
    if (!path || !out)
        return 0;
    std::memset(out, 0, sizeof(*out));

    FILE* f = AzalonDS::OpenFileUtf8(path, "rb");
    if (!f)
        return 0;
    u8 header[0x200]{};
    if (std::fread(header, 1, sizeof(header), f) != sizeof(header)) {
        std::fclose(f);
        return 0;
    }

    std::memcpy(out->header_title, header + 0x00, 12);
    std::memcpy(out->game_code, header + 0x0C, 4);
    for (char& c : out->header_title)
        if (c && (c < 0x20 || c > 0x7E))
            c = ' ';
    const u8 unit_code = header[0x12];
    out->is_dsi_enhanced = unit_code == 0x02;
    out->is_dsi_exclusive = unit_code == 0x03;
    u32 arm9_offset;
    std::memcpy(&arm9_offset, header + 0x20, 4);
    out->is_homebrew = arm9_offset < 0x4000 || std::memcmp(out->game_code, "####", 4) == 0;

    u32 banner_offset;
    std::memcpy(&banner_offset, header + 0x68, 4);
    std::string title;
    if (banner_offset != 0) {
        u8 banner[0x840]{};
        if (std::fseek(f, static_cast<long>(banner_offset), SEEK_SET) == 0 &&
            std::fread(banner, 1, sizeof(banner), f) == sizeof(banner)) {
            // Icon: 32x32, 4x4 tiles of 8x8 pixels, 4bpp, 16-colour BGR555 palette.
            u16 palette[16];
            std::memcpy(palette, banner + 0x220, sizeof(palette));
            for (int tile = 0; tile < 16; tile++) {
                for (int i = 0; i < 32; i++) {
                    const u8 byte = banner[0x20 + tile * 32 + i];
                    for (int nib = 0; nib < 2; nib++) {
                        const int index = nib ? (byte >> 4) : (byte & 0xF);
                        const int px = (tile % 4) * 8 + (i % 4) * 2 + nib;
                        const int py = (tile / 4) * 8 + i / 4;
                        u32 argb = 0;
                        if (index != 0) {
                            const u16 c = palette[index];
                            const u32 r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3,
                                      b = ((c >> 10) & 0x1F) << 3;
                            argb = 0xFF000000 | (r << 16) | (g << 8) | b;
                        }
                        out->icon[py * 32 + px] = argb;
                    }
                }
            }
            out->has_icon = 1;

            // Titles: 128 UTF-16 chars per language at 0x240; index 1 = English.
            char16_t text[128];
            std::memcpy(text, banner + 0x240 + 1 * 0x100, sizeof(text));
            title = Utf16ToUtf8(text, 128);
        }
    }
    std::fclose(f);

    if (title.empty())
        title = out->header_title;
    std::snprintf(out->title, sizeof(out->title), "%s", title.c_str());
    return 1;
}

} // extern "C"
