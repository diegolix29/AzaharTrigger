# Builds the AzalonDS DS/DSi core (src/ds_core: melonDS + the azds C API) as a separate
# CMake project and exposes it as the imported shared library target `azalon_ds`.
#
# Why a separate project: melonDS defines a CMake target named `core` (as Azahar does),
# melonDS cannot be compiled by MSVC, and both projects bundle different versions of
# teakra/xxhash. See src/ds_core/CMakeLists.txt.
#
# Use azalon_ds_copy_runtime(<target>) to place the library next to an executable.

include(ExternalProject)

set(AZDS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/ds_core")
set(AZDS_BINARY_DIR "${PROJECT_BINARY_DIR}/azalon_ds")
if (ANDROID AND CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    # The Android Gradle plugin packages every .so it finds here.
    set(AZDS_OUTPUT_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
else()
    set(AZDS_OUTPUT_DIR "${AZDS_BINARY_DIR}/out")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_azds_build_type Debug)
else()
    set(_azds_build_type Release)
endif()

set(_azds_args
    -DCMAKE_BUILD_TYPE=${_azds_build_type}
    -DAZDS_OUTPUT_DIR=${AZDS_OUTPUT_DIR}
    -DMELONDS_SOURCE_DIR=${PROJECT_SOURCE_DIR}/externals/melonDS
    -DAZALON_EXTERNALS_DIR=${PROJECT_SOURCE_DIR}/externals
)

if (MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # melonDS needs a GCC-compatible compiler. Use the clang that ships with Visual Studio
    # ("C++ Clang tools for Windows" component): same MSVC ABI and runtime as cl.
    string(REGEX REPLACE "/Tools/MSVC/.*$" "" _azds_vc_dir "${CMAKE_CXX_COMPILER}")
    set(_azds_llvm_hints
        "${_azds_vc_dir}/Tools/Llvm/x64/bin"
        "$ENV{VCINSTALLDIR}Tools/Llvm/x64/bin"
        "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin")
    find_program(AZDS_CLANG clang HINTS ${_azds_llvm_hints} NO_DEFAULT_PATH)
    find_program(AZDS_CLANGXX clang++ HINTS ${_azds_llvm_hints} NO_DEFAULT_PATH)
    find_program(AZDS_LLVM_RC llvm-rc HINTS ${_azds_llvm_hints} NO_DEFAULT_PATH)
    if (NOT AZDS_CLANG OR NOT AZDS_CLANGXX)
        message(FATAL_ERROR
            "AzalonDS: the DS core (melonDS) is compiled with clang on Windows. Install the "
            "'C++ Clang tools for Windows' component in the Visual Studio Installer, "
            "or configure with -DENABLE_DS=OFF.")
    endif()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_azds_runtime MultiThreadedDebugDLL)
    else()
        set(_azds_runtime MultiThreadedDLL)
    endif()
    list(APPEND _azds_args
        -DCMAKE_C_COMPILER=${AZDS_CLANG}
        -DCMAKE_CXX_COMPILER=${AZDS_CLANGXX}
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${_azds_runtime})
    if (AZDS_LLVM_RC)
        list(APPEND _azds_args -DCMAKE_RC_COMPILER=${AZDS_LLVM_RC})
    endif()
else()
    list(APPEND _azds_args
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
endif()

if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND _azds_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if (ANDROID)
    list(APPEND _azds_args
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_STL=${ANDROID_STL})
endif()
if (APPLE)
    list(APPEND _azds_args
        "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()
if (CMAKE_C_COMPILER_LAUNCHER)
    list(APPEND _azds_args
        -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
        -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER})
endif()

# Output file names.
if (WIN32)
    set(AZDS_RUNTIME_FILE "${AZDS_OUTPUT_DIR}/azalon_ds.dll")
    if (MINGW)
        set(AZDS_IMPLIB_FILE "${AZDS_OUTPUT_DIR}/libazalon_ds.dll.a")
    else()
        set(AZDS_IMPLIB_FILE "${AZDS_OUTPUT_DIR}/azalon_ds.lib")
    endif()
elseif (APPLE)
    set(AZDS_RUNTIME_FILE "${AZDS_OUTPUT_DIR}/libazalon_ds.dylib")
else()
    set(AZDS_RUNTIME_FILE "${AZDS_OUTPUT_DIR}/libazalon_ds.so")
endif()

ExternalProject_Add(azalon_ds_build
    SOURCE_DIR "${AZDS_SOURCE_DIR}"
    BINARY_DIR "${AZDS_BINARY_DIR}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS ${_azds_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target azalon_ds
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${AZDS_RUNTIME_FILE}" ${AZDS_IMPLIB_FILE}
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
)

add_library(azalon_ds SHARED IMPORTED GLOBAL)
set_target_properties(azalon_ds PROPERTIES
    IMPORTED_LOCATION "${AZDS_RUNTIME_FILE}"
    INTERFACE_INCLUDE_DIRECTORIES "${AZDS_SOURCE_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "ENABLE_DS")
if (AZDS_IMPLIB_FILE)
    set_target_properties(azalon_ds PROPERTIES IMPORTED_IMPLIB "${AZDS_IMPLIB_FILE}")
endif()
if (APPLE)
    set_target_properties(azalon_ds PROPERTIES IMPORTED_SONAME "@rpath/libazalon_ds.dylib")
endif()
add_dependencies(azalon_ds azalon_ds_build)

# Copies the DS core library next to <target>'s binary after each build.
function(azalon_ds_copy_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AZDS_RUNTIME_FILE}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying the AzalonDS DS core next to ${target}")
    if (UNIX AND NOT APPLE)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    elseif (APPLE)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@executable_path")
    endif()
endfunction()
