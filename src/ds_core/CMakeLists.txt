# AzalonDS DS/DSi core: melonDS + a small C API, built as a shared library.
#
# This is a standalone CMake project. Azahar builds it through ExternalProject
# (see CMakeModules/AzalonDSCore.cmake) because:
#   * melonDS's CMake defines a target named `core`, like Azahar does;
#   * melonDS does not compile with MSVC, so on Windows this project is built with
#     the clang that ships with Visual Studio (same MSVC ABI) while Azahar uses cl;
#   * melonDS and Azahar bundle different versions of teakra/xxhash. Building
#     melonDS into its own shared library with hidden symbols keeps them apart.
#
# It can also be configured on its own for quick iteration:
#   cmake -S src/ds_core -B build/ds_core -G Ninja && cmake --build build/ds_core

cmake_minimum_required(VERSION 3.20)
project(azalon_ds LANGUAGES C CXX)

get_filename_component(_azalon_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MELONDS_SOURCE_DIR "${_azalon_root}/externals/melonDS" CACHE PATH "melonDS source tree")
set(AZALON_EXTERNALS_DIR "${_azalon_root}/externals" CACHE PATH "Azahar externals folder")
set(AZDS_OUTPUT_DIR "" CACHE PATH "Where to put the built library (empty = build dir)")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Only the azds_* C API leaves the library.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if (AZDS_OUTPUT_DIR)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${AZDS_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${AZDS_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${AZDS_OUTPUT_DIR}")
endif()

if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
    if (NOT MINGW)
        # melonDS checks __WIN32__ (which MinGW defines) rather than _WIN32.
        add_compile_definitions(__WIN32__)
    endif()
endif()

# --- melonDS core (its own CMake, unmodified) --------------------------------

set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(USE_VCPKG OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_JIT_PROFILING OFF CACHE BOOL "" FORCE)
# TODO: OpenGL renderers (needs a GL loader shared with the frontend's context).
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)

if (WIN32 AND NOT MINGW)
    # FATStorage.cpp includes <dirent.h> without using it; MSVC-ABI toolchains lack it.
    set(DIRENT_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/compat" CACHE PATH "" FORCE)
endif()


add_subdirectory("${MELONDS_SOURCE_DIR}" melonds EXCLUDE_FROM_ALL)

# --- networking: melonDS net (LAN / local MP / slirp internet) ----------------

add_subdirectory("${MELONDS_SOURCE_DIR}/src/net/libslirp" libslirp EXCLUDE_FROM_ALL)

add_subdirectory("${AZALON_EXTERNALS_DIR}/enet" enet EXCLUDE_FROM_ALL)
target_include_directories(enet INTERFACE "${AZALON_EXTERNALS_DIR}/enet/include")

set(MELONDS_NET_DIR "${MELONDS_SOURCE_DIR}/src/net")
add_library(azds_net STATIC
    ${MELONDS_NET_DIR}/Net.cpp
    ${MELONDS_NET_DIR}/Net_PCap.cpp
    ${MELONDS_NET_DIR}/Net_Slirp.cpp
    ${MELONDS_NET_DIR}/PacketDispatcher.cpp
    ${MELONDS_NET_DIR}/LocalMP.cpp
    ${MELONDS_NET_DIR}/LAN.cpp
    ${MELONDS_NET_DIR}/MPInterface.cpp
)
target_include_directories(azds_net PUBLIC "${MELONDS_NET_DIR}" "${MELONDS_SOURCE_DIR}/src")
target_link_libraries(azds_net PUBLIC core slirp enet)
if (WIN32)
    target_compile_definitions(azds_net PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(azds_net PUBLIC ws2_32 iphlpapi winmm)
endif()

# --- the library ---------------------------------------------------------------

add_library(azalon_ds SHARED
    include/azalon_ds/azalon_ds.h
    ds_console.h
    console.cpp
    platform.cpp
    net.cpp
)
target_include_directories(azalon_ds
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${MELONDS_SOURCE_DIR}/src")
target_compile_definitions(azalon_ds PRIVATE AZDS_BUILDING_LIBRARY)
target_link_libraries(azalon_ds PRIVATE core azds_net)

if (WIN32)
    target_compile_definitions(azalon_ds PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_target_properties(azalon_ds PROPERTIES PREFIX "")
elseif (APPLE)
    set_target_properties(azalon_ds PROPERTIES INSTALL_NAME_DIR "@rpath")
else()
    # Keep the static libraries' symbols (teakra, xxhash, ...) out of the dynamic table.
    target_link_options(azalon_ds PRIVATE "-Wl,--exclude-libs,ALL" "-Wl,-Bsymbolic")
endif()

# --- smoke test -------------------------------------------------------------------

option(AZDS_BUILD_TESTS "Build the azds_smoke headless test tool" OFF)
if (AZDS_BUILD_TESTS)
    add_executable(azds_smoke tests/azds_smoke.c)
    target_link_libraries(azds_smoke PRIVATE azalon_ds)
    if (WIN32)
        target_compile_definitions(azds_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()
