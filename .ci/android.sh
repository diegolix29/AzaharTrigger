#!/bin/bash -ex

export NDK_CCACHE=$(which ccache)

if [ ! -z "${ANDROID_KEYSTORE_B64}" ]; then
    export ANDROID_KEYSTORE_FILE="${GITHUB_WORKSPACE}/ks.jks"
    base64 --decode <<< "${ANDROID_KEYSTORE_B64}" > "${ANDROID_KEYSTORE_FILE}"

    cat >> src/android/local.properties <<EOF
ANDROID_KEYSTORE=${ANDROID_KEYSTORE_FILE}
ANDROID_KEYSTORE_PASSWORD=${ANDROID_KEYSTORE_PASS}
ANDROID_KEY_ALIAS=dlix69
ANDROID_KEY_PASSWORD=${ANDROID_KEY_PASS:-$ANDROID_KEYSTORE_PASS}
EOF
fi

cd src/android
chmod +x ./gradlew
./gradlew assembleRelease
./gradlew bundleRelease

ccache -s -v

if [ ! -z "${ANDROID_KEYSTORE_B64}" ]; then
    rm "${ANDROID_KEYSTORE_FILE}"
    sed -i '/^ANDROID_KEYSTORE/d;/^ANDROID_KEY_ALIAS/d;/^ANDROID_KEY_PASSWORD/d' "${GITHUB_WORKSPACE}/src/android/local.properties" || true
fi
