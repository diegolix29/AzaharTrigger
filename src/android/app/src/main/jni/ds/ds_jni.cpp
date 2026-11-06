// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// JNI entry points of the Android DS frontend:
//   org.citra.citra_emu.ds.DsNative   - the running console
//   org.citra.citra_emu.ds.DsRomInfo  - .nds metadata for the game list

#include <memory>
#include <string>

#include <jni.h>

#include <android/native_window_jni.h>

#include <azalon_ds/azalon_ds.h>

#include "common/logging/log.h"
#include "jni/android_common/android_common.h"
#include "jni/ds/ds_session.h"
#include "jni/id_cache.h"

namespace {

// DsNative.SCREEN_WIDTH / SCREEN_HEIGHT are plain Kotlin constants, so the view
// layout keeps working when this build has no DS core at all.
static_assert(AZDS_SCREEN_WIDTH == 256 && AZDS_SCREEN_HEIGHT == 192,
              "DsNative.SCREEN_WIDTH/SCREEN_HEIGHT need updating");

jclass s_ds_native_class = nullptr;
jmethodID s_on_stopped = nullptr;

jobject MakeRomInfo(JNIEnv* env, const azds_rom_info& info) {
    jclass clazz = env->FindClass("org/citra/citra_emu/ds/DsRomInfo");
    if (!clazz) {
        return nullptr;
    }
    jmethodID ctor =
        env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;Ljava/lang/String;[IZZZ)V");
    if (!ctor) {
        return nullptr;
    }

    // Fall back to the 12-character header title for ROMs without a banner.
    std::string title = info.title;
    if (title.empty()) {
        title = info.header_title;
        while (!title.empty() && title.back() == ' ') {
            title.pop_back();
        }
    }

    jintArray icon = nullptr;
    if (info.has_icon) {
        icon = env->NewIntArray(32 * 32);
        if (icon) {
            env->SetIntArrayRegion(icon, 0, 32 * 32,
                                   reinterpret_cast<const jint*>(info.icon));
        }
    }

    return env->NewObject(clazz, ctor, ToJString(env, title), ToJString(env, info.game_code), icon,
                          static_cast<jboolean>(info.is_dsi_enhanced != 0),
                          static_cast<jboolean>(info.is_dsi_exclusive != 0),
                          static_cast<jboolean>(info.is_homebrew != 0));
}

} // namespace

namespace DS {

void NotifyStopped() {
    if (!s_ds_native_class || !s_on_stopped) {
        return;
    }
    JNIEnv* env = IDCache::GetEnvForThread();
    if (!env) {
        return;
    }
    env->CallStaticVoidMethod(s_ds_native_class, s_on_stopped);
}

} // namespace DS

extern "C" {

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_nativeInit(JNIEnv* env,
                                                                        jclass clazz) {
    if (!s_ds_native_class) {
        s_ds_native_class = static_cast<jclass>(env->NewGlobalRef(clazz));
        s_on_stopped = env->GetStaticMethodID(clazz, "onEmulationStopped", "()V");
    }
    if (azds_api_version() != AZDS_API_VERSION) {
        LOG_WARNING(Frontend, "[DS] core API version {} != frontend {}", azds_api_version(),
                    AZDS_API_VERSION);
    }
    LOG_INFO(Frontend, "[DS] core ready (melonDS {})", azds_melonds_version());
}

JNIEXPORT jstring JNICALL Java_org_citra_citra_1emu_ds_DsNative_nativeStart(
    JNIEnv* env, jclass, jstring rom_path, jstring save_path, jstring data_dir, jint console_type,
    jboolean direct_boot, jboolean jit, jboolean threaded_renderer, jstring nickname,
    jint language, jint favorite_color, jstring ds_bios9, jstring ds_bios7, jstring ds_firmware,
    jstring dsi_bios9, jstring dsi_bios7, jstring dsi_firmware, jstring dsi_nand,
    jboolean dldi_enable, jstring dldi_sync_folder, jint wireless_mode, jboolean internet) {
    DS::StartParams params;
    params.rom_path = GetJString(env, rom_path);
    params.save_path = GetJString(env, save_path);
    params.data_dir = GetJString(env, data_dir);
    params.console_type = console_type;
    params.direct_boot = direct_boot;
    params.jit = jit;
    params.threaded_renderer = threaded_renderer;
    params.nickname = GetJString(env, nickname);
    params.language = language;
    params.favorite_color = favorite_color;
    params.ds_bios9 = GetJString(env, ds_bios9);
    params.ds_bios7 = GetJString(env, ds_bios7);
    params.ds_firmware = GetJString(env, ds_firmware);
    params.dsi_bios9 = GetJString(env, dsi_bios9);
    params.dsi_bios7 = GetJString(env, dsi_bios7);
    params.dsi_firmware = GetJString(env, dsi_firmware);
    params.dsi_nand = GetJString(env, dsi_nand);
    params.dldi_enable = dldi_enable;
    params.dldi_sync_folder = GetJString(env, dldi_sync_folder);
    params.wireless_mode = wireless_mode;
    params.internet = internet;

    std::string error;
    if (DS::Session::Get().Start(params, &error)) {
        return nullptr;
    }
    if (error.empty()) {
        error = "Unknown error";
    }
    return ToJString(env, error);
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_stop(JNIEnv*, jclass) {
    DS::Session::Get().Stop();
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsNative_isRunning(JNIEnv*, jclass) {
    return DS::Session::Get().IsRunning();
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsNative_isPaused(JNIEnv*, jclass) {
    return DS::Session::Get().IsPaused();
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_setPaused(JNIEnv*, jclass,
                                                                       jboolean paused) {
    DS::Session::Get().SetPaused(paused);
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_reset(JNIEnv*, jclass) {
    DS::Session::Get().Reset();
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_setFastForward(JNIEnv*, jclass,
                                                                            jboolean enabled) {
    DS::Session::Get().SetFastForward(enabled);
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsNative_saveState(JNIEnv* env, jclass,
                                                                           jstring path) {
    return DS::Session::Get().SaveState(GetJString(env, path));
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_ds_DsNative_loadState(JNIEnv* env, jclass,
                                                                           jstring path) {
    return DS::Session::Get().LoadState(GetJString(env, path));
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_setSurface(JNIEnv* env, jclass,
                                                                        jint screen,
                                                                        jobject surface) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    DS::Session::Get().SetSurface(static_cast<DS::Screen>(screen), window);
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_setKeys(JNIEnv*, jclass, jint mask) {
    DS::Session::Get().SetKeys(static_cast<std::uint32_t>(mask));
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_setTouch(JNIEnv*, jclass,
                                                                      jboolean pressed, jint x,
                                                                      jint y) {
    DS::Session::Get().SetTouch(pressed, x, y);
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_ds_DsNative_setLidClosed(JNIEnv*, jclass,
                                                                          jboolean closed) {
    DS::Session::Get().SetLidClosed(closed);
}

JNIEXPORT jdouble JNICALL Java_org_citra_citra_1emu_ds_DsNative_getFps(JNIEnv*, jclass) {
    return DS::Session::Get().Fps();
}

JNIEXPORT jobject JNICALL Java_org_citra_citra_1emu_ds_DsRomInfo_probe(JNIEnv* env, jclass,
                                                                       jstring path) {
    const std::string native_path = GetJString(env, path);
    azds_rom_info info{};
    if (!azds_probe_rom(native_path.c_str(), &info)) {
        return nullptr;
    }
    return MakeRomInfo(env, info);
}

} // extern "C"
