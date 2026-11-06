// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// melonDS::Platform implementation for AzalonDS. melonDS passes the `void* userdata`
// given to the NDS constructor back into these functions; for us that is the
// owning azds_console.

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "Platform.h"
#include "SPI_Firmware.h"
#include "net/MPInterface.h"

#include "ds_console.h"

namespace fs = std::filesystem;

namespace {

azds_console* Console(void* userdata) {
    return static_cast<azds_console*>(userdata);
}

int InstanceOf(void* userdata) {
    auto* console = Console(userdata);
    return console ? console->instance_id : 0;
}

fs::path PathFromUtf8(const std::string& path) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(path.begin(), path.end()));
#else
    return fs::u8path(path);
#endif
}

const auto kStartTime = std::chrono::steady_clock::now();

} // namespace

namespace AzalonDS {

FILE* OpenFileUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    const auto wide_path = PathFromUtf8(path).wstring();
    wchar_t wide_mode[8]{};
    for (size_t i = 0; i < 7 && mode[i]; i++)
        wide_mode[i] = static_cast<wchar_t>(mode[i]);
    return _wfopen(wide_path.c_str(), wide_mode);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

} // namespace AzalonDS

namespace melonDS::Platform {

// ---------------------------------------------------------------------------
// Lifecycle

void SignalStop(StopReason reason, void* userdata) {
    auto* console = Console(userdata);
    if (console && console->config.callbacks.on_stop)
        console->config.callbacks.on_stop(console->config.callbacks.userdata,
                                          static_cast<int>(reason));
}

// ---------------------------------------------------------------------------
// Files

static std::string ModeString(FileMode mode, bool file_exists) {
    std::string result;
    if (mode & FileMode::Append)
        result += 'a';
    else if (!(mode & FileMode::Write))
        result += 'r';
    else if (mode & FileMode::NoCreate)
        result += 'r'; // "r+" below
    else if ((mode & FileMode::Preserve) && file_exists)
        result += 'r'; // "r+" below
    else
        result += 'w';

    if ((mode & FileMode::ReadWrite) == FileMode::ReadWrite)
        result += '+';
    if (!(mode & FileMode::Text))
        result += 'b';
    return result;
}

std::string GetLocalFilePath(const std::string& filename) {
    if (filename.empty())
        return filename;
    const fs::path path = PathFromUtf8(filename);
    if (path.is_absolute() || AzalonDS::GetDataDir().empty())
        return filename;
    const fs::path full = PathFromUtf8(AzalonDS::GetDataDir()) / path;
    const auto u8 = full.u8string();
    return std::string(u8.begin(), u8.end());
}

FileHandle* OpenFile(const std::string& path, FileMode mode) {
    if ((mode & (FileMode::ReadWrite | FileMode::Append)) == FileMode::None) {
        Log(LogLevel::Error, "Attempted to open \"%s\" in neither read nor write mode\n",
            path.c_str());
        return nullptr;
    }
    if (path.empty())
        return nullptr;

    std::error_code ec;
    const bool exists = fs::exists(PathFromUtf8(path), ec);
    if ((mode & FileMode::NoCreate) && !exists)
        return nullptr;

    return reinterpret_cast<FileHandle*>(
        AzalonDS::OpenFileUtf8(path, ModeString(mode, exists).c_str()));
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) {
    return OpenFile(GetLocalFilePath(path), mode);
}

bool FileExists(const std::string& name) {
    std::error_code ec;
    return !name.empty() && fs::is_regular_file(PathFromUtf8(name), ec);
}

bool LocalFileExists(const std::string& name) {
    return FileExists(GetLocalFilePath(name));
}

bool CheckFileWritable(const std::string& filepath) {
    FileHandle* file = OpenFile(filepath, FileMode::Append);
    if (!file)
        return false;
    CloseFile(file);
    return true;
}

bool CheckLocalFileWritable(const std::string& filepath) {
    return CheckFileWritable(GetLocalFilePath(filepath));
}

bool CloseFile(FileHandle* file) {
    return std::fclose(reinterpret_cast<FILE*>(file)) == 0;
}

bool IsEndOfFile(FileHandle* file) {
    return std::feof(reinterpret_cast<FILE*>(file)) != 0;
}

bool FileReadLine(char* str, int count, FileHandle* file) {
    return std::fgets(str, count, reinterpret_cast<FILE*>(file)) != nullptr;
}

u64 FilePosition(FileHandle* file) {
#ifdef _WIN32
    return static_cast<u64>(_ftelli64(reinterpret_cast<FILE*>(file)));
#else
    return static_cast<u64>(ftello(reinterpret_cast<FILE*>(file)));
#endif
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin) {
    int whence = SEEK_SET;
    switch (origin) {
    case FileSeekOrigin::Start:
        whence = SEEK_SET;
        break;
    case FileSeekOrigin::Current:
        whence = SEEK_CUR;
        break;
    case FileSeekOrigin::End:
        whence = SEEK_END;
        break;
    }
#ifdef _WIN32
    return _fseeki64(reinterpret_cast<FILE*>(file), offset, whence) == 0;
#else
    return fseeko(reinterpret_cast<FILE*>(file), offset, whence) == 0;
#endif
}

void FileRewind(FileHandle* file) {
    std::rewind(reinterpret_cast<FILE*>(file));
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) {
    return std::fread(data, size, count, reinterpret_cast<FILE*>(file));
}

bool FileFlush(FileHandle* file) {
    return std::fflush(reinterpret_cast<FILE*>(file)) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) {
    return std::fwrite(data, size, count, reinterpret_cast<FILE*>(file));
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...) {
    if (!fmt)
        return 0;
    va_list args;
    va_start(args, fmt);
    const int ret = std::vfprintf(reinterpret_cast<FILE*>(file), fmt, args);
    va_end(args);
    return ret < 0 ? 0 : static_cast<u64>(ret);
}

u64 FileLength(FileHandle* file) {
    FILE* f = reinterpret_cast<FILE*>(file);
    const u64 pos = FilePosition(file);
    FileSeek(file, 0, FileSeekOrigin::End);
    const u64 len = FilePosition(file);
    FileSeek(file, static_cast<s64>(pos), FileSeekOrigin::Start);
    (void)f;
    return len;
}

// ---------------------------------------------------------------------------
// Logging

void Log(LogLevel level, const char* fmt, ...) {
    if (!fmt)
        return;
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::string message(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    if (message.empty())
        return;
    AzalonDS::LogMessage(static_cast<azds_log_level>(level), message);
}

// ---------------------------------------------------------------------------
// Threads

struct Thread {
    std::thread thread;
};

Thread* Thread_Create(std::function<void()> func) {
    auto* thread = new Thread;
    thread->thread = std::thread(std::move(func));
    return thread;
}

void Thread_Free(Thread* thread) {
    if (!thread)
        return;
    if (thread->thread.joinable())
        thread->thread.detach();
    delete thread;
}

void Thread_Wait(Thread* thread) {
    if (thread && thread->thread.joinable())
        thread->thread.join();
}

struct Semaphore {
    std::mutex lock;
    std::condition_variable cond;
    int count = 0;
};

Semaphore* Semaphore_Create() {
    return new Semaphore;
}

void Semaphore_Free(Semaphore* sema) {
    delete sema;
}

void Semaphore_Reset(Semaphore* sema) {
    std::lock_guard guard(sema->lock);
    sema->count = 0;
}

void Semaphore_Wait(Semaphore* sema) {
    std::unique_lock guard(sema->lock);
    sema->cond.wait(guard, [sema] { return sema->count > 0; });
    sema->count--;
}

bool Semaphore_TryWait(Semaphore* sema, int timeout_ms) {
    std::unique_lock guard(sema->lock);
    if (timeout_ms <= 0) {
        if (sema->count <= 0)
            return false;
    } else if (!sema->cond.wait_for(guard, std::chrono::milliseconds(timeout_ms),
                                    [sema] { return sema->count > 0; })) {
        return false;
    }
    sema->count--;
    return true;
}

void Semaphore_Post(Semaphore* sema, int count) {
    {
        std::lock_guard guard(sema->lock);
        sema->count += count;
    }
    if (count == 1)
        sema->cond.notify_one();
    else
        sema->cond.notify_all();
}

struct Mutex {
    std::mutex mutex;
};

Mutex* Mutex_Create() {
    return new Mutex;
}

void Mutex_Free(Mutex* mutex) {
    delete mutex;
}

void Mutex_Lock(Mutex* mutex) {
    mutex->mutex.lock();
}

void Mutex_Unlock(Mutex* mutex) {
    mutex->mutex.unlock();
}

bool Mutex_TryLock(Mutex* mutex) {
    return mutex->mutex.try_lock();
}

void Sleep(u64 usecs) {
    std::this_thread::sleep_for(std::chrono::microseconds(usecs));
}

u64 GetMSCount() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - kStartTime)
                                .count());
}

u64 GetUSCount() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - kStartTime)
                                .count());
}

// ---------------------------------------------------------------------------
// Saves

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen,
                  void* userdata) {
    auto* console = Console(userdata);
    if (console && console->nds_save)
        console->nds_save->RequestFlush(savedata, savelen, writeoffset, writelen);
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen,
                  void* userdata) {
    auto* console = Console(userdata);
    if (console && console->gba_save)
        console->gba_save->RequestFlush(savedata, savelen, writeoffset, writelen);
}

void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata) {
    auto* console = Console(userdata);
    if (!console || !console->firmware_save)
        return;

    if (firmware.GetHeader().Identifier != GENERATED_FIRMWARE_IDENTIFIER) {
        // Real firmware dump: write the whole thing back.
        console->firmware_save->RequestFlush(firmware.Buffer(), firmware.Length(), writeoffset,
                                             writelen);
        return;
    }

    // Generated firmware: only persist the Wi-Fi access points (Nintendo WFC settings).
    const u32 eapstart = firmware.GetExtendedAccessPointOffset();
    const u32 apstart = firmware.GetWifiAccessPointOffset();
    const u32 apend = apstart + sizeof(firmware.GetAccessPoints());
    if (eapstart <= writeoffset && writeoffset < apend) {
        const u8* buffer = firmware.GetExtendedAccessPointPosition();
        const u32 length =
            sizeof(firmware.GetExtendedAccessPoints()) + sizeof(firmware.GetAccessPoints());
        console->firmware_save->RequestFlush(buffer, length, writeoffset - eapstart, writelen);
    }
}

void WriteDateTime(int year, int month, int day, int hour, int minute, int second,
                   void* userdata) {
    auto* console = Console(userdata);
    if (!console)
        return;
    std::tm when{};
    when.tm_year = year - 1900;
    when.tm_mon = month - 1;
    when.tm_mday = day;
    when.tm_hour = hour;
    when.tm_min = minute;
    when.tm_sec = second;
    when.tm_isdst = -1;
    const std::time_t guest = std::mktime(&when);
    const std::time_t host = std::time(nullptr);
    if (guest != static_cast<std::time_t>(-1))
        console->rtc_offset = static_cast<long long>(guest) - static_cast<long long>(host);
}

// ---------------------------------------------------------------------------
// Local wireless (MP) and internet

void MP_Begin(void* userdata) {
    MPInterface::Get().Begin(InstanceOf(userdata));
}

void MP_End(void* userdata) {
    MPInterface::Get().End(InstanceOf(userdata));
}

int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata) {
    return MPInterface::Get().SendPacket(InstanceOf(userdata), data, len, timestamp);
}

int MP_RecvPacket(u8* data, u64* timestamp, void* userdata) {
    return MPInterface::Get().RecvPacket(InstanceOf(userdata), data, timestamp);
}

int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) {
    return MPInterface::Get().SendCmd(InstanceOf(userdata), data, len, timestamp);
}

int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) {
    return MPInterface::Get().SendReply(InstanceOf(userdata), data, len, timestamp, aid);
}

int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) {
    return MPInterface::Get().SendAck(InstanceOf(userdata), data, len, timestamp);
}

int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) {
    return MPInterface::Get().RecvHostPacket(InstanceOf(userdata), data, timestamp);
}

u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) {
    return MPInterface::Get().RecvReplies(InstanceOf(userdata), data, timestamp, aidmask);
}

int Net_SendPacket(u8* data, int len, void* userdata) {
    return AzalonDS::NetSendPacket(data, len, InstanceOf(userdata));
}

int Net_RecvPacket(u8* data, void* userdata) {
    return AzalonDS::NetRecvPacket(data, InstanceOf(userdata));
}

// ---------------------------------------------------------------------------
// Camera / microphone

void Camera_Start(int num, void* userdata) {}

void Camera_Stop(int num, void* userdata) {}

void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata) {
    auto* console = Console(userdata);
    if (console && console->config.callbacks.camera_capture) {
        console->config.callbacks.camera_capture(console->config.callbacks.userdata, num, frame,
                                                 width, height, yuv ? 1 : 0);
        return;
    }
    // No camera: produce a black frame.
    const int pixels = yuv ? (width / 2) * height : width * height;
    for (int i = 0; i < pixels; i++)
        frame[i] = yuv ? 0x80008000 : 0xFF000000;
}

void Mic_Start(void* userdata) {
    if (auto* console = Console(userdata))
        console->mic_active = true;
}

void Mic_Stop(void* userdata) {
    if (auto* console = Console(userdata))
        console->mic_active = false;
}

int Mic_ReadInput(s16* data, int maxlength, void* userdata) {
    auto* console = Console(userdata);
    if (console && console->config.callbacks.mic_read)
        return console->config.callbacks.mic_read(console->config.callbacks.userdata, data,
                                                  maxlength);
    std::memset(data, 0, sizeof(s16) * static_cast<size_t>(maxlength));
    return maxlength;
}

// ---------------------------------------------------------------------------
// DSi AAC (used by a handful of DSiWare titles through the DSP HLE).
// TODO: decode with faad2 (Azahar already bundles it).

struct AACDecoder {};

AACDecoder* AAC_Init() {
    return nullptr;
}

void AAC_DeInit(AACDecoder* dec) {
    delete dec;
}

bool AAC_Configure(AACDecoder* dec, int frequency, int channels) {
    return false;
}

bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output,
                     int outputlen) {
    return false;
}

// ---------------------------------------------------------------------------
// GBA slot add-ons

bool Addon_KeyDown(KeyType type, void* userdata) {
    return false;
}

void Addon_RumbleStart(u32 len, void* userdata) {
    auto* console = Console(userdata);
    if (console && console->config.callbacks.rumble_start)
        console->config.callbacks.rumble_start(console->config.callbacks.userdata, len);
}

void Addon_RumbleStop(void* userdata) {
    auto* console = Console(userdata);
    if (console && console->config.callbacks.rumble_stop)
        console->config.callbacks.rumble_stop(console->config.callbacks.userdata);
}

float Addon_MotionQuery(MotionQueryType type, void* userdata) {
    // Motion Pak at rest: 1G pointing down.
    return type == MotionAccelerationZ ? 1.0f : 0.0f;
}

// ---------------------------------------------------------------------------
// Dynamic libraries (used to load wpcap/libpcap for the pcap network driver)

struct DynamicLibrary {};

DynamicLibrary* DynamicLibrary_Load(const char* lib) {
#ifdef _WIN32
    return reinterpret_cast<DynamicLibrary*>(LoadLibraryA(lib));
#else
    return reinterpret_cast<DynamicLibrary*>(dlopen(lib, RTLD_NOW | RTLD_LOCAL));
#endif
}

void DynamicLibrary_Unload(DynamicLibrary* lib) {
    if (!lib)
        return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) {
    if (!lib)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

} // namespace melonDS::Platform
