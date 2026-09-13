# Building Crysis VR on Linux (clang-cl cross compile)

The mod is a Windows DLL built with Visual Studio. On Linux it can be cross-compiled with
LLVM's MSVC-compatible toolchain: `clang-cl` (compiler), `lld-link` (linker) and `llvm-rc`
(resource compiler), using the Microsoft CRT and Windows SDK downloaded by
[xwin](https://github.com/Jake-Shadle/xwin).

Nothing here needs root. The toolchain lives in `~/.local/opt` (override with `XBUILD_OPT`).

## 1. Install the toolchain (once)

```sh
tools/xbuild/setup_toolchain.sh
```

This downloads about 3 GB:

| Component | Version | Location |
|---|---|---|
| LLVM (clang-cl, lld-link, llvm-rc) | 23.1.1 | `~/.local/opt/llvm` |
| MSVC CRT + Windows SDK (via xwin) | MSVC 14.44, SDK 10.0.26100 | `~/.local/opt/winsysroot` |
| CMake | 4.4.3 | `~/.local/opt/cmake` |
| Ninja | 1.13.2 | `~/.local/bin/ninja` |

xwin downloads the CRT and SDK from Microsoft; running the script accepts
[Microsoft's license terms](https://go.microsoft.com/fwlink/?LinkId=2086102).

The official LLVM Linux release is built on Ubuntu 22.04, so `lld-link` and `llvm-mt` need
ICU 70. The script bundles those libraries in `~/.local/opt/llvm-compat` and puts small
wrappers there; newer distributions (e.g. Ubuntu 26.04) need them.

## 2. Build VRMod.dll

```sh
tools/xbuild/build_vrmod.py              # both Bin32/VRMod.dll and Bin64/VRMod.dll
tools/xbuild/build_vrmod.py --arch x86   # 32-bit only (the one the Quest/WinlatorXR uses)
tools/xbuild/build_vrmod.py -k           # keep going and list every compile error
tools/xbuild/build_vrmod.py --warnings   # show compiler warnings (hidden by default)
tools/xbuild/build_vrmod.py --clean      # rebuild from scratch
```

The source list, precompiled-header settings and per-file exclusions are read from
`Code/GameDll.vcxproj`, so adding a file in Visual Studio is enough for the Linux build too.
Compiler and linker flags mirror MSBuild's Release|Win32 and Release|x64 command lines.
Intermediate files go to `BinTemp/xbuild/<arch>`, outputs to `Bin32` / `Bin64`.

To copy the result into a Crysis install or a package folder (like `install.bat`):

```sh
tools/xbuild/build_vrmod.py --install /path/to/Crysis
# or: CRYSIS_INSTALL_DIR=/path/to/Crysis tools/xbuild/build_vrmod.py
```

## 3. Build the launcher (CrysisVR.exe)

Only needed when `Code/ThirdParty/c1-launcher` changes:

```sh
tools/xbuild/build_launcher.sh        # Bin32 and Bin64
tools/xbuild/build_launcher.sh x86
```

It uses the CMake toolchain file `tools/xbuild/clang-cl-toolchain.cmake`, which also works
for other MSVC-style CMake projects.

## 4. Quest package

`Quest/make_quest_package.sh [version]` is the Linux counterpart of `Quest/make_quest_package.bat`:
it builds both DLLs, assembles `Quest/CrysisVR-Installer/` and writes
`Quest/CrysisVR-Installer-Beta-<version>.zip`. It packages `Bin32/CrysisVR.exe` and
`Bin64/CrysisVR.exe` as they are.

## Limitations

- **Shaders:** `Code/Shaders/*.hlsl` are compiled to `Code/Shaders/generated/*.h` by `fxc` during
  the Windows build. Linux has no `fxc`, so the build uses the existing generated headers and warns
  when a `.hlsl` file has uncommitted changes. After changing a shader, regenerate its header on
  Windows (or with `fxc.exe` under Wine).
- **clang vs. MSVC:** clang is stricter than MSVC about some constructs in the 2007 CryEngine SDK.
  The few places that needed it are guarded with `#if defined(__clang__)` or fixed in a way that
  also compiles with MSVC:
  - include name case (`TArray.h`, `imgui.h`), since Linux file systems are case-sensitive
  - `Cry_XOptimise.h`: the SSE `cryMemcpy` uses MASM `$labels` clang cannot assemble; clang uses `memcpy`
  - `IGameFramework.h`: the extension factory's covariant `Create()` return type
  - pointer `<= 0` comparisons, `char*` from string literals, `CryString` passed as `SFlashVarValue`
  - `ScreenEffects.h`: `CLinearBlend()` left `m_slope` uninitialised (now 1.0 as intended)
  - c1-launcher `EXELoader.cpp`: function pointer compared with `void*`
- **Verification:** the cross-built `VRMod.dll` and `CrysisVR.exe` have the same exports, imported
  DLLs, image base and security flags as the MSVC builds. They are not bit-identical (different
  compiler), so test in game before releasing.
- **Debug CRT:** xwin installs only the release CRT, so only Release builds are supported.
