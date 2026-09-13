# Crysis VR Mod

## Compiling

Install Crysis. Set up the environment variable `CRYSIS_INSTALL_DIR` to point to where Crysis is installed.

Download and install [Visual Studio 2022](https://visualstudio.microsoft.com). Be sure to install C++ dev tools.
Open the `Code\CrysisMod.sln` solution and build it.

## Compiling on Linux

The mod can be cross-compiled on Linux with clang-cl, lld-link and the Microsoft CRT/Windows SDK
fetched by xwin. No root access is required:

```sh
tools/xbuild/setup_toolchain.sh   # once: installs the toolchain into ~/.local/opt
tools/xbuild/build_vrmod.py       # builds Bin32/VRMod.dll and Bin64/VRMod.dll
```

See [tools/xbuild/README.md](tools/xbuild/README.md) for options, the launcher build and limitations.
