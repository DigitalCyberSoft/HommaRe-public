#!/bin/sh
# Build innoextract as a static library for the iOS launcher, which installs a
# game from the player's GOG offline installer in-process (an iOS app cannot
# run the Mac's helper executable).
#
#   kit/third_party/innoextract/build-ios.sh <output-dir> [iphoneos|iphonesimulator ...]
#
# Writes <output-dir>/<sdk>/librecomp_innoextract.a (arm64) for each SDK
# (both by default): innoextract 1.9 with the Mac helper's patches and
# lib-patches/, lib/recomp_innoextract.cpp for an entry point instead of
# main(), Boost 1.89's Filesystem and Iostreams sources and liblzma (XZ Utils)
# compiled in. The app links the system's libz, libbz2 and libiconv itself.
# Every source archive is pinned by SHA-256; downloads are kept in
# <output-dir>/downloads for the next build.
set -eu

INNO_VERSION=1.9
INNO_SHA256=6344a69fc1ed847d4ed3e272e0da5998948c6b828cb7af39c6321aba6cf88126
INNO_URL="https://constexpr.org/innoextract/files/innoextract-$INNO_VERSION.tar.gz"
BOOST_DIR=boost_1_89_0
BOOST_SHA256=85a33fa22621b4f314f8e85e1a5e2a9363d22e4f4992925d4bb3bc631b5a0c7a
BOOST_URL="https://archives.boost.io/release/1.89.0/source/$BOOST_DIR.tar.bz2"
XZ_VERSION=5.8.1
XZ_SHA256=507825b599356c10dca1cd720c9d0d0c9d5400b9de300af00e4d1ea150795543
XZ_URL="https://github.com/tukaani-project/xz/releases/download/v$XZ_VERSION/xz-$XZ_VERSION.tar.gz"
# The app's own minimum (cmake/IosBundle.cmake).
IOS_MIN=17.0

here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build-ios.sh <output-dir> [iphoneos|iphonesimulator ...]}
shift
sdks=${*:-iphoneos iphonesimulator}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cache="$out/downloads"
work="$out/build"
mkdir -p "$cache"

fetch() { # url sha256 file
    if [ -f "$cache/$3" ] && echo "$2  $cache/$3" | shasum -a 256 -c - >/dev/null 2>&1; then
        return
    fi
    curl -fsSL -o "$cache/$3.part" "$1"
    echo "$2  $cache/$3.part" | shasum -a 256 -c - >/dev/null
    mv "$cache/$3.part" "$cache/$3"
}
fetch "$INNO_URL" "$INNO_SHA256" "innoextract-$INNO_VERSION.tar.gz"
fetch "$BOOST_URL" "$BOOST_SHA256" "$BOOST_DIR.tar.bz2"
fetch "$XZ_URL" "$XZ_SHA256" "xz-$XZ_VERSION.tar.gz"

rm -rf "$work"
mkdir -p "$work/src"
tar -xzf "$cache/innoextract-$INNO_VERSION.tar.gz" -C "$work/src"
tar -xzf "$cache/xz-$XZ_VERSION.tar.gz" -C "$work/src"
# Only the headers and the two libraries' sources (the archive is 150 MB).
tar -xjf "$cache/$BOOST_DIR.tar.bz2" -C "$work/src" "$BOOST_DIR/boost" \
    "$BOOST_DIR/libs/filesystem/src" "$BOOST_DIR/libs/iostreams/src" "$BOOST_DIR/LICENSE_1_0.txt"
inno="$work/src/innoextract-$INNO_VERSION"
for p in "$here"/patches/*.patch "$here"/lib-patches/*.patch; do
    patch -s -p1 -d "$inno" < "$p"
done

for sdk in $sdks; do
    case $sdk in
        iphoneos) min="-miphoneos-version-min=$IOS_MIN" ;;
        iphonesimulator) min="-mios-simulator-version-min=$IOS_MIN" ;;
        *) echo "unknown SDK $sdk" >&2; exit 1 ;;
    esac
    sysroot=$(xcrun --sdk "$sdk" --show-sdk-path)
    common="-G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS
        -DCMAKE_OSX_SYSROOT=$sysroot -DCMAKE_OSX_ARCHITECTURES=arm64
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$IOS_MIN -DCMAKE_C_FLAGS=$min"

    # liblzma alone: no command line tools, translations or docs.
    # shellcheck disable=SC2086
    cmake -S "$work/src/xz-$XZ_VERSION" -B "$work/xz-$sdk" $common -DBUILD_SHARED_LIBS=OFF \
        -DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF \
        -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_SANDBOX=no >/dev/null
    cmake --build "$work/xz-$sdk" --target liblzma >/dev/null

    # shellcheck disable=SC2086
    cmake -S "$here/lib" -B "$work/lib-$sdk" $common -DINNOEXTRACT_SRC="$inno" \
        -DCMAKE_CXX_FLAGS=$min -DBOOST_SRC="$work/src/$BOOST_DIR" -DLZMA_INCLUDE="$work/src/xz-$XZ_VERSION/src/liblzma/api" >/dev/null
    cmake --build "$work/lib-$sdk" >/dev/null

    mkdir -p "$out/$sdk"
    libtool -static -no_warning_for_no_symbols -o "$out/$sdk/librecomp_innoextract.a" \
        "$work/lib-$sdk/librecomp_innoextract.a" "$work/xz-$sdk/liblzma.a"
    echo "built $out/$sdk/librecomp_innoextract.a ($(du -h "$out/$sdk/librecomp_innoextract.a" | cut -f1))"
done
cp "$here/lib/recomp_innoextract.h" "$out/recomp_innoextract.h"
rm -rf "$work"
