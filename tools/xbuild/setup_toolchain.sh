#!/usr/bin/env bash
# Installs a user-space Windows cross toolchain for building Crysis VR on Linux (no sudo needed):
#   LLVM (clang-cl, lld-link, llvm-rc), CMake, Ninja, xwin + Microsoft CRT and Windows SDK.
#
# Everything goes to ~/.local/opt (override with XBUILD_OPT) and ~/.local/bin.
# NOTE: xwin downloads the MSVC CRT and Windows SDK from Microsoft and requires accepting
# Microsoft's license terms (--accept-license): https://go.microsoft.com/fwlink/?LinkId=2086102
set -euo pipefail

OPT="${XBUILD_OPT:-$HOME/.local/opt}"
BIN="$HOME/.local/bin"
DL="$OPT/downloads"
mkdir -p "$OPT" "$BIN" "$DL"

LLVM_VER="${LLVM_VER:-23.1.1}"
CMAKE_VER="${CMAKE_VER:-4.4.3}"
NINJA_VER="${NINJA_VER:-1.13.2}"
XWIN_VER="${XWIN_VER:-0.10.0}"

fetch() { # url file
  if [ ! -s "$DL/$2" ]; then
    echo "[download] $2"
    curl -fL --retry 3 --progress-bar -o "$DL/$2.part" "$1"
    mv "$DL/$2.part" "$DL/$2"
  else
    echo "[download] $2 (cached)"
  fi
}

fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VER/ninja-linux.zip" "ninja-$NINJA_VER.zip"
fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-x86_64.tar.gz" "cmake-$CMAKE_VER.tar.gz"
fetch "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VER/xwin-$XWIN_VER-x86_64-unknown-linux-musl.tar.gz" "xwin-$XWIN_VER.tar.gz"
fetch "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/LLVM-$LLVM_VER-Linux-X64.tar.xz" "llvm-$LLVM_VER.tar.xz"

echo "[install] ninja"
unzip -o -q "$DL/ninja-$NINJA_VER.zip" -d "$BIN"
chmod +x "$BIN/ninja"

echo "[install] cmake"
if [ ! -x "$OPT/cmake-$CMAKE_VER/bin/cmake" ]; then
  rm -rf "$OPT/cmake-$CMAKE_VER" "$OPT/cmake-$CMAKE_VER-linux-x86_64"
  tar -xzf "$DL/cmake-$CMAKE_VER.tar.gz" -C "$OPT"
  mv "$OPT/cmake-$CMAKE_VER-linux-x86_64" "$OPT/cmake-$CMAKE_VER"
fi
ln -sfn "$OPT/cmake-$CMAKE_VER" "$OPT/cmake"

echo "[install] xwin"
if [ ! -x "$OPT/xwin-$XWIN_VER/xwin" ]; then
  rm -rf "$OPT/xwin-$XWIN_VER"
  mkdir -p "$OPT/xwin-$XWIN_VER"
  tar -xzf "$DL/xwin-$XWIN_VER.tar.gz" -C "$OPT/xwin-$XWIN_VER" --strip-components=1
fi
ln -sfn "$OPT/xwin-$XWIN_VER/xwin" "$BIN/xwin"

echo "[install] llvm $LLVM_VER (large, takes a few minutes)"
if [ ! -x "$OPT/llvm-$LLVM_VER/bin/clang" ]; then
  rm -rf "$OPT/llvm-$LLVM_VER"
  mkdir -p "$OPT/llvm-$LLVM_VER"
  tar -xJf "$DL/llvm-$LLVM_VER.tar.xz" -C "$OPT/llvm-$LLVM_VER" --strip-components=1
fi
ln -sfn "$OPT/llvm-$LLVM_VER" "$OPT/llvm"

# The LLVM release binaries are built on Ubuntu 22.04: lld-link and llvm-mt link against ICU 70,
# which newer distributions no longer ship. Bundle ICU 70 privately and wrap those two tools.
echo "[install] ICU 70 compatibility libraries for lld-link / llvm-mt"
COMPAT="$OPT/llvm-compat"
mkdir -p "$COMPAT/lib" "$COMPAT/bin"
if ! ldd "$(readlink -f "$OPT/llvm/bin/lld-link")" | grep -q "not found"; then
  echo "  system libraries suffice, no wrapper needed"
else
  if [ ! -e "$COMPAT/lib/libicuuc.so.70" ]; then
    fetch "http://archive.ubuntu.com/ubuntu/pool/main/i/icu/libicu70_70.1-2ubuntu1_amd64.deb" "libicu70.deb"
    rm -rf "$DL/icu70"
    dpkg-deb -x "$DL/libicu70.deb" "$DL/icu70"
    cp -a "$DL"/icu70/usr/lib/x86_64-linux-gnu/libicu*.so.70* "$COMPAT/lib/"
    rm -rf "$DL/icu70"
  fi
fi
for t in lld-link llvm-mt; do
  real="$(readlink -f "$OPT/llvm/bin/$t")"
  cat > "$COMPAT/bin/$t" <<EOF
#!/bin/bash
# The LLVM release binaries are built on Ubuntu 22.04 and need ICU 70 (bundled in ../lib).
LD_LIBRARY_PATH="$COMPAT/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" exec -a $t "$real" "\$@"
EOF
  chmod +x "$COMPAT/bin/$t"
done

echo "[install] MSVC CRT + Windows SDK via xwin (x86, x86_64)"
if [ ! -d "$OPT/winsysroot/VC" ]; then
  "$BIN/xwin" --accept-license --arch x86,x86_64 --cache-dir "$DL/xwin-cache" \
    splat --use-winsysroot-style --preserve-ms-arch-notation --output "$OPT/winsysroot"
fi

echo
echo "[ok] toolchain ready"
"$OPT/llvm/bin/clang-cl" --version | head -1
"$OPT/llvm/bin/lld-link" --version | head -1
"$OPT/cmake/bin/cmake" --version | head -1
echo "ninja $("$BIN/ninja" --version)"
echo "winsysroot: $OPT/winsysroot"
