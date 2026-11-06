# AzalonDS

DS + DSi + 3DS emulator: the user's Azahar fork (AzaharTrigger, 3DS) with the user's
melonDS fork (TriggerDS) merged in as a real second core. Targets every platform Azahar
ships (Windows, Linux, macOS, Android, libretro); Windows desktop is wired up first.

This repo (`F:\REPOS\Azalon`, branch `master`) was created from AzaharTrigger's
`AzaharTrigger` branch. Remotes: `trigger` = diegolix29/AzaharTrigger, `azahar` = azahar-emu/azahar.

**Do not use AzaharTrigger's `AzaLon3DS` branch** (old libretro melonds-ds attempt). The user
wants a clean core-to-core merge so melonDS's own wireless (LAN/local) and internet work.

## Layout

| Path | What |
|---|---|
| `externals/melonDS` | submodule → diegolix29/melonDS-Trigger-Desktop, branch `TriggerDS` (user's melonDS fork; its CMake is used unmodified) |
| `src/ds_core/` | **azalon_ds**: standalone CMake project, melonDS core + a plain C API, built as a shared library |
| `src/ds_core/include/azalon_ds/azalon_ds.h` | the only header the frontend sees (C types only) |
| `src/ds_core/console.cpp` | console lifetime, boot, frames, input, audio, saves, savestates, AR cheats, ROM probe (title/icon) |
| `src/ds_core/platform.cpp` | `melonDS::Platform` implementation (files, threads, saves, MP/Net glue, mic/camera/rumble hooks) |
| `src/ds_core/net.cpp` | wireless: `MPInterface` Local/LAN (+ lobby API); internet: `melonDS::Net` + libslirp |
| `src/ds_core/tests/azds_smoke.c` | headless boot test, writes a PNG (`-DAZDS_BUILD_TESTS=ON`) |
| `CMakeModules/AzalonDSCore.cmake` | builds src/ds_core via ExternalProject → imported target `azalon_ds`; `azalon_ds_copy_runtime(<exe>)` |
| `src/citra_qt/ds/` | Qt frontend for DS: `Session` (emu thread, pacing, Azahar input profile + audio sink), `ScreenWidget`, `Config` + `ConfigDialog` (`<config>/azalon_ds.ini`), `ds_lan.*` (DS LAN tab of Azahar's Lobby, host/direct-connect dialogs, room window, status-bar button) |
| `src/android/app/src/main/jni/ds/` | Android native DS layer: `Session` (emu thread, pacing, two `ANativeWindow`s, audio via `AudioCore::Sink`) + `ds_jni.cpp` (`DsNative`, `DsRomInfo`) |
| `src/android/.../citra_emu/ds/` | Android Kotlin DS frontend: `DsEmulationActivity`, `DsScreensView`/`DsScreenView`, `DsInputOverlay`, `DsSettings`(+Activity, `res/xml/ds_preferences.xml`), `DsPaths`, `DsRom` |
| `tools/build-windows-msvc.bat` | configure + build with vcvars (`x64-release` / `x64-debug`) |

Hooks in Azahar code are all under `#ifdef ENABLE_DS` (CMake option `ENABLE_DS`, default ON):
`citra_qt.cpp` (BootGame/OnStopGame/OnPauseContinueGame/ConfirmChangeGame/closeEvent,
"DS" menu, file filter), `citra_qt.h` (members), `game_list_worker.cpp` (.nds rows with banner
title/icon, synthetic program id `"NDS"+gamecode`), `citra_qt/CMakeLists.txt`,
`citra_meta/CMakeLists.txt` (copies `azalon_ds.dll` next to `azahar.exe`).
`ENABLE_DS` is a PUBLIC define of citra_qt because it changes `GMainWindow`'s layout.

## Why azalon_ds is a separate shared library
- melonDS's CMake defines a target named `core`, like Azahar.
- melonDS does not compile with MSVC (GCC extensions, GAS `.S` JIT stubs). On Windows the
  library is built with **Visual Studio's bundled clang** (`VC/Tools/Llvm/x64/bin/clang++`,
  MSVC ABI + /MD runtime) while Azahar stays on cl. Needs the VS component
  "C++ Clang tools for Windows".
- Azahar and melonDS bundle different, incompatible teakra and xxhash. Inside the DLL/.so
  everything but `azds_*` is hidden (`-fvisibility=hidden`, `--exclude-libs,ALL` on ELF).
- melonDS's OpenGL renderer is OFF for now (software renderer, threaded). Enabling it needs a
  GL loader/context shared with the frontend.
- On MSVC-ABI builds melonDS needs `__WIN32__` defined and a dummy `dirent.h` (src/ds_core/compat).

## Building (Windows)
```
tools\build-windows-msvc.bat x64-release
```
Output: `out\build\x64-release\bin\Release\azahar.exe` + `azalon_ds.dll`.
FFmpeg is off (`-DENABLE_FFMPEG=OFF`, like the user's previous builds). Bundled Qt 6.9.3 is
downloaded by CMake into `out/build/<preset>/externals/qt` (on this machine that folder is a
junction to AzaharTrigger's already-downloaded copy).

Quick iteration on the DS core alone (VS clang, Ninja from VS's CMake folder):
```
cmake -S src/ds_core -B build/ds_core-standalone -G Ninja -DCMAKE_C_COMPILER=<VS>/VC/Tools/Llvm/x64/bin/clang.exe -DCMAKE_CXX_COMPILER=<...>/clang++.exe -DAZDS_BUILD_TESTS=ON
build\ds_core-standalone\azds_smoke.exe game.nds 600 out.png
```

## Source repositories this was merged from (user's, GitHub `diegolix29`)
| Role | Local path | Branch |
|---|---|---|
| 3DS base | `F:\REPOS\AzaharTrigger\AzaharTrigger` | `AzaharTrigger` |
| DS desktop | `C:\msys64\home\DiegoMuget\melonDS` | `TriggerDS` (= externals/melonDS) |
| DS Android | `F:\REPOS\melonDS-android-master\melonDS-android` | `TriggerDS` |

The two melonDS copies (desktop vs `melonDS-android-lib`) differ only slightly: GLES renderer
tweaks (`GPU*_OpenGL*`, `OpenGLSupport`), ~50 lines in `net/LAN.cpp`, small Wifi/DSi bits.
The desktop core is the single shared core; fold the Android deltas in when porting Android.
The Android repo is owned by another Windows SID: use `git -c safe.directory=* ...`.

## Status / next steps
- Done: azalon_ds library (DS + DSi, JIT, saves, savestates, AR cheats, DLDI SD, LAN + local
  wireless, slirp internet), Windows MSVC build integration, Qt DS session/screen/menu/settings,
  DS games in the game list.
- DS LAN has ONE home: Azahar's Multiplayer UI (desktop: Multiplayer > DS Local Wireless (LAN)
  submenu + "DS Local Wireless (LAN)" tab in the Lobby window; Android: DS tab of `NetPlayDialog`,
  screens in `ds/DsNetPlay.kt` built from Azahar's multiplayer layouts). The old standalone
  reimplementation `src/network/lan_melon` ("Mandarine", never connected to the core) was removed.
  Rooms carry host name (in `description`), current game + in-game flag (`azds_lan_set_game`,
  set by the frontends when a DS game starts/stops), chat with sender names. Nickname = Azahar's
  multiplayer nickname on both platforms.
- Done (Android, builds with `gradlew assembleVanillaRelWithDebInfo`; not run on a device yet): `.nds/.srl/.dsi/.ids` in
  the game list with banner title+icon, `DsEmulationActivity` (software renderer blitted to two
  SurfaceViews, on-screen controls, physical pads, touch screen, savestates, layouts, lid),
  `DsSettingsActivity` (BIOS/firmware/NAND picked with the file picker or imported from a
  folder, copied to `<ds dir>/bios/` by `DsBios`), `azalon_ds` linked into `citra-android`;
  home screen entries for DS settings and booting the DS/DSi firmware menu (needs real dumps).
  DS LAN glue: `DsLan` (JNI in `jni/ds/ds_lan_jni.cpp`), in-game join/leave/chat toasts.
- LAN between desktop and Android: same core and protocol (UDP 7063 discovery, 7064 session).
  Checked host/join/password/chat with two desktop processes; not yet tested phone <-> PC.
- Not done: OpenGL/upscaled DS rendering, DS hotkeys, DS play-time tracking, AAC (DSi) decoding
  (faad2), mic/camera passthrough, multi-instance local MP in one process, per-game DS settings, libretro, Linux/macOS packaging of the shared library
  (rpath/bundle), CI (`.github/workflows/build.yml`).

## Android notes
- The DS core opens files with stdio, so SAF documents are passed to it as
  `/proc/self/fd/<fd>`; `DsRom.Handle` owns that descriptor for the session's lifetime.
- DS saves/states live in `<Azahar user dir>/ds/{saves,states}` (same layout as desktop), or in
  the app's external files dir when the user directory is not a writable real path.
- `PreferenceFragmentCompat` needs `preferenceTheme`, added to `Theme.Citra` in
  `res/values/themes.xml`.
- `libazalon_ds.so` is emitted into `CMAKE_LIBRARY_OUTPUT_DIRECTORY` so the Android Gradle
  plugin packages it; `libcitra-android.so` has a DT_NEEDED on it.

## Working notes
- Shell: PowerShell primary; Git Bash available (no cmake on its PATH: use VS's
  `Common7/IDE/CommonExtensions/Microsoft/CMake/{CMake/bin,Ninja}` or the .bat).
- Don't push or rewrite branches in the source repos without asking.
