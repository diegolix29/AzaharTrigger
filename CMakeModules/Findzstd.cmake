# libzip looks up zstd with its own Find module, then always links
# zstd::libzstd_static unless it is building shared. MXE's shared toolchain
# only provides the import library, so that static target is never created.
# This wrapper runs first and points the static name at the shared library.

include("${CMAKE_CURRENT_LIST_DIR}/../externals/libzip/cmake/Findzstd.cmake")

if(zstd_FOUND AND TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static ALIAS zstd::libzstd_shared)
endif()
