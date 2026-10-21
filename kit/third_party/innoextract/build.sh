#!/bin/sh
# Build the innoextract helper the macOS launcher runs to install a game from
# its GOG offline installer (setup_*.exe + *-N.bin, Inno Setup).
#
#   kit/third_party/innoextract/build.sh <output-dir>
#
# Fetches the pinned innoextract release, applies the patches in patches/
# (upstream commits Homebrew carries for CMake 4 and Boost 1.85/1.89) and links
# Boost and liblzma statically, so the binary needs only the system's libz,
# libbz2, libiconv and libc++. Boost and XZ Utils are built from source for
# the app's oldest macOS (MACOSX_DEPLOYMENT_TARGET, 14.0 by default, as in
# kit/CMakeLists.txt): Homebrew's are built for the Mac they were installed on.
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
MACOS_MIN=${MACOSX_DEPLOYMENT_TARGET:-14.0}
export MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN

here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build.sh <output-dir>}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cache="$out/downloads"
work="$out/innoextract-build"
deps="$work/deps"
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
mkdir -p "$work/src" "$deps"
tar -xzf "$cache/innoextract-$INNO_VERSION.tar.gz" -C "$work/src"
tar -xzf "$cache/xz-$XZ_VERSION.tar.gz" -C "$work/src"
tar -xjf "$cache/$BOOST_DIR.tar.bz2" -C "$work/src"
src="$work/src/innoextract-$INNO_VERSION"
for p in "$here"/patches/*.patch; do
    patch -s -p1 -d "$src" < "$p"
done
min="-mmacosx-version-min=$MACOS_MIN"

# liblzma alone: no command line tools, translations or docs.
cmake -S "$work/src/xz-$XZ_VERSION" -B "$work/xz" -G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" -DCMAKE_INSTALL_PREFIX="$deps" -DBUILD_SHARED_LIBS=OFF \
    -DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF \
    -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_SANDBOX=no >/dev/null
cmake --build "$work/xz" --target install >/dev/null

# The Boost libraries innoextract links (patches/03: System is header-only
# since 1.69). Boost.Iostreams gets the system's zlib and bzip2 and none of
# the compressors innoextract does not use.
boost="$work/src/$BOOST_DIR"
(cd "$boost" && ./bootstrap.sh --with-toolset=clang >/dev/null &&
    ./b2 -q -d0 -j"$(sysctl -n hw.ncpu)" --prefix="$deps" \
        --with-iostreams --with-filesystem --with-date_time --with-program_options \
        toolset=clang link=static runtime-link=shared variant=release threading=multi \
        cxxflags="$min" cflags="$min" linkflags="$min" \
        -sNO_LZMA=1 -sNO_ZSTD=1 install >/dev/null)

cmake -S "$src" -B "$work/build" -G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
    -DUSE_STATIC_LIBS=OFF -DBoost_USE_STATIC_LIBS=ON -DLZMA_USE_STATIC_LIBS=ON \
    -DCMAKE_PREFIX_PATH="$deps" -DBoost_NO_SYSTEM_PATHS=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-dead_strip_dylibs" >/dev/null
cmake --build "$work/build" >/dev/null
cp "$work/build/innoextract" "$out/innoextract"
if otool -L "$out/innoextract" | tail -n +2 | grep -v '^[[:space:]]*/usr/lib/' >/dev/null; then
    echo "innoextract links libraries outside /usr/lib:" >&2
    otool -L "$out/innoextract" >&2
    exit 1
fi
rm -rf "$work"
echo "built $out/innoextract ($("$out/innoextract" --version | head -1), macOS $MACOS_MIN+)"
