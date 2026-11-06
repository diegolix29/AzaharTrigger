#!/bin/bash -ex

if [ "$GITHUB_REF_TYPE" == "tag" ]; then
	export EXTRA_CMAKE_FLAGS=(-DENABLE_QT_UPDATE_CHECKER=ON)
fi

# FFmpeg is linked directly into Azahar now; CI builds it with Conan (see build.yml).
CONAN_TOOLCHAIN="$HOME/conan/conan_toolchain.cmake"
if [ -f "$CONAN_TOOLCHAIN" ]; then
    export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" -DCMAKE_TOOLCHAIN_FILE="$CONAN_TOOLCHAIN")
else
    echo "::warning::No Conan FFmpeg toolchain found, building without FFmpeg"
    export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" -DENABLE_FFMPEG=OFF)
fi

mkdir -p build
cd build
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$TARGET" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DENABLE_QT_TRANSLATION=ON \
    -DENABLE_ROOM_STANDALONE=OFF \
    -DUSE_DISCORD_PRESENCE=ON \
	"${EXTRA_CMAKE_FLAGS[@]}"
ninja
ninja bundle
mv ./bundle/azahar.app ./bundle/Azahar.app # TODO: Can this be done in CMake?

ccache -s -v

CURRENT_ARCH=`arch`
if [ "$TARGET" = "$CURRENT_ARCH" ]; then
  ctest -VV -C Release
fi
