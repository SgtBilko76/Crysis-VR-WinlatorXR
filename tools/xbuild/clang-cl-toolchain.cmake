# CMake toolchain file: cross-compile Windows binaries on Linux with clang-cl + lld-link + llvm-rc
# against the MSVC CRT / Windows SDK installed by tools/xbuild/setup_toolchain.sh.
#
#   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/xbuild/clang-cl-toolchain.cmake -DXBUILD_ARCH=x86 ...
#
# XBUILD_ARCH: x86 (default) or x64.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)

if(NOT XBUILD_ARCH)
	set(XBUILD_ARCH x86)
endif()
if(XBUILD_ARCH STREQUAL "x64")
	set(CMAKE_SYSTEM_PROCESSOR AMD64)
	set(_xbuild_target x86_64-pc-windows-msvc)
	set(_xbuild_machine x64)
else()
	set(CMAKE_SYSTEM_PROCESSOR X86)
	set(_xbuild_target i686-pc-windows-msvc)
	set(_xbuild_machine x86)
endif()

if(NOT XBUILD_OPT)
	if(DEFINED ENV{XBUILD_OPT})
		set(XBUILD_OPT "$ENV{XBUILD_OPT}")
	else()
		set(XBUILD_OPT "$ENV{HOME}/.local/opt")
	endif()
endif()
set(XBUILD_LLVM "${XBUILD_OPT}/llvm")
set(XBUILD_WINSYSROOT "${XBUILD_OPT}/winsysroot")

# make the cache variables visible to try_compile projects
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XBUILD_ARCH XBUILD_OPT)

set(CMAKE_C_COMPILER "${XBUILD_LLVM}/bin/clang-cl")
set(CMAKE_CXX_COMPILER "${XBUILD_LLVM}/bin/clang-cl")
set(CMAKE_RC_COMPILER "${XBUILD_LLVM}/bin/llvm-rc")
# lld-link / llvm-mt from the LLVM release need ICU 70; setup_toolchain.sh provides wrappers
if(EXISTS "${XBUILD_OPT}/llvm-compat/bin/lld-link")
	set(CMAKE_LINKER "${XBUILD_OPT}/llvm-compat/bin/lld-link")
	set(CMAKE_MT "${XBUILD_OPT}/llvm-compat/bin/llvm-mt")
else()
	set(CMAKE_LINKER "${XBUILD_LLVM}/bin/lld-link")
	set(CMAKE_MT "${XBUILD_LLVM}/bin/llvm-mt")
endif()
set(CMAKE_AR "${XBUILD_LLVM}/bin/llvm-lib")
set(CMAKE_C_COMPILER_TARGET ${_xbuild_target})
set(CMAKE_CXX_COMPILER_TARGET ${_xbuild_target})

# /winsysroot gives clang-cl and lld-link the CRT + SDK include and library paths for the target arch
file(GLOB _xbuild_msvc LIST_DIRECTORIES true "${XBUILD_WINSYSROOT}/VC/Tools/MSVC/*")
list(SORT _xbuild_msvc)
list(GET _xbuild_msvc -1 _xbuild_msvc)
get_filename_component(_xbuild_msvc_ver "${_xbuild_msvc}" NAME)
string(REGEX REPLACE "^14\\.([0-9]+).*" "19.\\1" _xbuild_compat "${_xbuild_msvc_ver}")

set(_xbuild_cflags "/winsysroot \"${XBUILD_WINSYSROOT}\" -fms-compatibility-version=${_xbuild_compat}")
set(CMAKE_C_FLAGS_INIT "${_xbuild_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_xbuild_cflags}")
foreach(_kind EXE SHARED MODULE)
	set(CMAKE_${_kind}_LINKER_FLAGS_INIT "/winsysroot:\"${XBUILD_WINSYSROOT}\" /machine:${_xbuild_machine}")
endforeach()

# llvm-rc does not understand /winsysroot, so give it the SDK include directories explicitly
file(GLOB _xbuild_sdk LIST_DIRECTORIES true "${XBUILD_WINSYSROOT}/Windows Kits/10/Include/*")
list(SORT _xbuild_sdk)
list(GET _xbuild_sdk -1 _xbuild_sdk)
set(CMAKE_RC_FLAGS_INIT "/I \"${_xbuild_sdk}/um\" /I \"${_xbuild_sdk}/shared\" /I \"${_xbuild_sdk}/ucrt\" /I \"${_xbuild_msvc}/include\"")

# xwin only ships the release CRT (no msvcrtd.lib / libcmtd.lib), so CMake's compiler checks must not
# use the Debug configuration
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
