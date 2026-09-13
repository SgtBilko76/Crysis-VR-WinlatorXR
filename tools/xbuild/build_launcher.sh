#!/usr/bin/env bash
# Cross-compiles the c1-launcher (CrysisVR.exe) for 32 and/or 64 bit with clang-cl on Linux.
# Its own post-build step copies the exe into the repo's Bin32 / Bin64 folders.
#
#   tools/xbuild/build_launcher.sh [x86|x64|all]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OPT="${XBUILD_OPT:-$HOME/.local/opt}"
CMAKE="$OPT/cmake/bin/cmake"
NINJA="$(command -v ninja || echo "$HOME/.local/bin/ninja")"
ARCH="${1:-all}"

[ -x "$CMAKE" ] || { echo "error: $CMAKE missing - run tools/xbuild/setup_toolchain.sh" >&2; exit 1; }
[ -x "$NINJA" ] || { echo "error: ninja missing - run tools/xbuild/setup_toolchain.sh" >&2; exit 1; }

build() { # arch bindir
  local arch="$1" bindir="$2"
  local build="$REPO/BinTemp/xbuild/launcher-$arch"
  mkdir -p "$REPO/$bindir"
  "$CMAKE" -S "$REPO/Code/ThirdParty/c1-launcher" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/tools/xbuild/clang-cl-toolchain.cmake" \
    -DXBUILD_ARCH="$arch" -DXBUILD_OPT="$OPT" \
    -DCMAKE_BUILD_TYPE=Release
  "$CMAKE" --build "$build" --target CrysisVR
  echo "[ok] $bindir/CrysisVR.exe"
}

case "$ARCH" in
  x86) build x86 Bin32 ;;
  x64) build x64 Bin64 ;;
  all) build x86 Bin32; build x64 Bin64 ;;
  *) echo "usage: $0 [x86|x64|all]" >&2; exit 2 ;;
esac
