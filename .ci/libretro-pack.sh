#!/bin/bash -ex

# Determine the full revision name.
GITDATE="`git show -s --date=short --format='%ad' | sed 's/-//g'`"
GITREV="`git show -s --format='%h'`"

REV_NAME="azahar-libretro-$OS-$TARGET-$GITDATE-$GITREV"
if [ "$GITHUB_REF_TYPE" = "tag" ]; then
    REV_NAME="azahar-libretro-$OS-$TARGET-$GITHUB_REF_NAME"
fi

# Handle different filename patterns for different platforms
if [ "$OS" = "android" ]; then
    zip -j -9 $REV_NAME.zip $BUILD_DIR/$EXTRA_PATH/azahar_libretro_android.so
else
    zip -j -9 $REV_NAME.zip $BUILD_DIR/$EXTRA_PATH/azahar_libretro.*
fi