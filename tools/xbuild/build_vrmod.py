#!/usr/bin/env python3
"""Cross-compile VRMod.dll on Linux with clang-cl + lld-link (see tools/xbuild/README.md).

The source list, per-file precompiled-header settings and exclusions are read from
Code/GameDll.vcxproj, so the Visual Studio project stays the single source of truth. The
compiler and linker flags mirror the Release|Win32 and Release|x64 command lines MSBuild used.

Usage:
  tools/xbuild/build_vrmod.py [--arch x86|x64|all] [--jobs N] [--warnings] [--clean]
                              [--install DIR]  (or CRYSIS_INSTALL_DIR=DIR)
"""
import argparse
import glob
import os
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CODE = os.path.join(REPO, 'Code')
VCXPROJ = os.path.join(CODE, 'GameDll.vcxproj')
OPT = os.environ.get('XBUILD_OPT', os.path.expanduser('~/.local/opt'))
LLVM = os.environ.get('XBUILD_LLVM', os.path.join(OPT, 'llvm'))
WINSYSROOT = os.environ.get('XBUILD_WINSYSROOT', os.path.join(OPT, 'winsysroot'))
MSBUILD_NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'

INCLUDE_DIRS = ['.', 'CryEngine/CryCommon', 'CryEngine/CryAction', 'ThirdParty/minhook/include',
                'ThirdParty/openxr/include', 'ThirdParty/imgui', 'ThirdParty/bhaptics/include/shared',
                'ThirdParty/ForceTubeVR/include']

COMMON_DEFINES = ['GAMEDLL_EXPORTS', '_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS', 'NTDDI_VERSION=NTDDI_WIN7',
                  '_VC80_UPGRADE=0x0710', '_WINDLL', '_MBCS']

ARCHES = {
    'x86': {
        'platform': 'Win32',
        'target': 'i686-pc-windows-msvc',
        'machine': 'X86',
        'bin': 'Bin32',
        'defines': COMMON_DEFINES,
        # MSBuild Release|Win32: /Ox /Ob2 /Oi /Ot /Oy- /GF /MD /GS- /arch:SSE /fp:fast /GR- (no /EH)
        'cflags': ['/Ox', '/Ob2', '/Oi', '/Ot', '/Oy-', '/GF', '/MD', '/GS-', '/arch:SSE', '/fp:fast',
                   '/Zc:wchar_t', '/Zc:forScope', '/Zc:inline', '/GR-', '/Gd'],
        'libdirs': ['ThirdParty/openxr/lib/win32', 'ThirdParty/bhaptics/bin/win32', 'ThirdParty/ForceTubeVR/bin'],
        'libs': ['ForceTubeVR_API_x32.lib'],
        'ldflags': ['/LARGEADDRESSAWARE', '/OPT:REF', '/OPT:ICF', '/SAFESEH'],
        'pdb': 'CrysisMod.pdb',
        'install_script_libs': ('Code/ThirdParty/bhaptics/bin/win32/haptic_library.dll',
                                'Code/ThirdParty/ForceTubeVR/bin/ForceTubeVR_API_x32.dll'),
    },
    'x64': {
        'platform': 'x64',
        'target': 'x86_64-pc-windows-msvc',
        'machine': 'X64',
        'bin': 'Bin64',
        'defines': COMMON_DEFINES[:1] + ['NOT_USE_CRY_MEMORY_MANAGER'] + COMMON_DEFINES[1:],
        # MSBuild Release|x64: /O2 /Ob2 /Oi /Oy /GF /EHsc /MD /GS- /Gy- /fp:precise /wd4091
        'cflags': ['/O2', '/Ob2', '/Oi', '/Oy', '/GF', '/EHsc', '/MD', '/GS-', '/Gy-', '/fp:precise',
                   '/Zc:wchar_t', '/Zc:forScope', '/Zc:inline', '/Gd', '/wd4091'],
        'libdirs': ['ThirdParty/openxr/lib', 'ThirdParty/bhaptics/bin/win64', 'ThirdParty/ForceTubeVR/bin'],
        'libs': ['ForceTubeVR_API_x64.lib'],
        'ldflags': ['/OPT:REF', '/OPT:NOICF'],
        'pdb': 'VRMod.pdb',
        'install_script_libs': ('Code/ThirdParty/bhaptics/bin/win64/haptic_library.dll',
                                'Code/ThirdParty/ForceTubeVR/bin/ForceTubeVR_API_x64.dll'),
    },
}

LIBS = ['dxgi.lib', 'd3d9.lib', 'd3d10_1.lib', 'd3d11.lib', 'openxr_loader.lib', 'ws2_32.lib', 'haptic_library.lib']
DEFAULT_LIBS = ['kernel32.lib', 'user32.lib', 'gdi32.lib', 'winspool.lib', 'comdlg32.lib', 'advapi32.lib',
                'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'odbc32.lib', 'odbccp32.lib']


def die(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(1)


class CaseInsensitivePaths:
    """Resolves the Windows-style (case-insensitive, backslash) paths used in the .vcxproj."""

    def __init__(self, root):
        self.root = root
        self.index = {}
        for dirpath, dirnames, filenames in os.walk(root):
            for name in dirnames + filenames:
                full = os.path.join(dirpath, name)
                self.index.setdefault(os.path.relpath(full, root).lower(), full)

    def resolve(self, winpath):
        rel = os.path.normpath(winpath.replace('\\', '/'))
        exact = os.path.join(self.root, rel)
        if os.path.exists(exact):
            return exact
        return self.index.get(rel.lower())


def config_value(elem, tag, platform):
    """Value of a per-configuration child element for Release|<platform>, or None."""
    wanted = "'$(Configuration)|$(Platform)'=='Release|%s'" % platform
    for child in elem.findall(MSBUILD_NS + tag):
        cond = child.get('Condition')
        if cond is None or cond.replace(' ', '') == wanted.replace(' ', ''):
            return (child.text or '').strip()
    return None


def read_project(platform):
    tree = ET.parse(VCXPROJ)
    paths = CaseInsensitivePaths(CODE)
    sources = []
    for item in tree.getroot().iter(MSBUILD_NS + 'ClCompile'):
        include = item.get('Include')
        if not include:
            continue
        if (config_value(item, 'ExcludedFromBuild', platform) or '').lower() == 'true':
            continue
        path = paths.resolve(include)
        if not path:
            die('source listed in GameDll.vcxproj not found: ' + include)
        pch = config_value(item, 'PrecompiledHeader', platform) or 'Use'
        sources.append((path, pch))
    resources = []
    for item in tree.getroot().iter(MSBUILD_NS + 'ResourceCompile'):
        path = paths.resolve(item.get('Include'))
        if not path:
            die('resource listed in GameDll.vcxproj not found: ' + item.get('Include'))
        resources.append(path)
    return sources, resources


def msvc_compat_version():
    tools = sorted(glob.glob(os.path.join(WINSYSROOT, 'VC', 'Tools', 'MSVC', '*')))
    if not tools:
        die('no MSVC toolset in %s - run tools/xbuild/setup_toolchain.sh first' % WINSYSROOT)
    minor = int(os.path.basename(tools[-1]).split('.')[1])  # 14.44.x -> 19.44
    return '19.%d' % minor


def sdk_include_dirs():
    inc = sorted(glob.glob(os.path.join(WINSYSROOT, 'Windows Kits', '10', 'Include', '*')))
    if not inc:
        die('no Windows SDK includes in ' + WINSYSROOT)
    sdk = inc[-1]
    msvc = sorted(glob.glob(os.path.join(WINSYSROOT, 'VC', 'Tools', 'MSVC', '*', 'include')))[-1]
    return [os.path.join(sdk, d) for d in ('um', 'shared', 'ucrt', 'winrt')] + [msvc]


def ninja_escape(path):
    return path.replace('$', '$$').replace(' ', '$ ').replace(':', '$:')


def q(arg):
    return shlex.quote(arg)


def rsp_quote(arg):
    """Quote for an lld-link response file, which uses Windows command-line rules, not shell rules."""
    if not arg or any(c in arg for c in ' \t"'):
        return '"' + arg.replace('\\', '\\\\').replace('"', '\\"') + '"'
    return arg


def obj_name(src):
    rel = os.path.relpath(src, CODE)
    return re.sub(r'[\\/]', '_', os.path.splitext(rel)[0]) + '.obj'


def write_ninja(arch, args):
    cfg = ARCHES[arch]
    sources, resources = read_project(cfg['platform'])
    build_dir = os.path.join(REPO, 'BinTemp', 'xbuild', arch)
    out_dir = os.path.join(REPO, cfg['bin'])
    os.makedirs(build_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)

    clang_cl = os.path.join(LLVM, 'bin', 'clang-cl')
    # lld-link needs ICU 70 from the Ubuntu 22.04 based LLVM release; setup_toolchain.sh wraps it
    lld_link = os.path.join(OPT, 'llvm-compat', 'bin', 'lld-link')
    if not os.path.exists(lld_link):
        lld_link = os.path.join(LLVM, 'bin', 'lld-link')
    llvm_rc = os.path.join(LLVM, 'bin', 'llvm-rc')
    for tool in (clang_cl, lld_link, llvm_rc):
        if not os.path.exists(tool):
            die('%s missing - run tools/xbuild/setup_toolchain.sh first' % tool)

    base = ['--target=' + cfg['target'], '/winsysroot', WINSYSROOT,
            '-fms-compatibility-version=' + msvc_compat_version(),
            '/nologo', '/c', '/Z7', '/FC']
    base += ['/I' + os.path.join(CODE, d) for d in INCLUDE_DIRS]
    base += ['/D' + d for d in cfg['defines']]
    base += cfg['cflags']
    if args.warnings:
        base += ['/W3']
    else:
        base += ['/w']  # the 2007 CryEngine SDK produces thousands of warnings with clang
    # clang is stricter than MSVC 2022 about a few legacy constructs the CryEngine SDK relies on
    base += ['-Wno-error=incompatible-function-pointer-types', '-Wno-error=int-conversion',
             # MSVC accepts narrowing constant conversions in case labels / braced initialisers
             '-Wno-c++11-narrowing']

    pch_header = 'StdAfx.h'
    pch_file = os.path.join(build_dir, 'VRMod.pch')

    lines = []
    w = lines.append
    w('ninja_required_version = 1.8')
    w('msvc_deps_prefix = Note: including file:')
    w('builddir = ' + ninja_escape(build_dir))
    w('cflags = ' + ' '.join(q(a) for a in base))
    w('')
    w('rule cc')
    w('  command = %s $cflags $extra /showIncludes /Fo$out $in' % q(clang_cl))
    w('  deps = msvc')
    w('  description = CC [%s] $shortname' % arch)
    w('')
    w('rule rc')
    rc_inc = ' '.join('/I ' + q(d) for d in [CODE] + sdk_include_dirs())
    w('  command = %s /nologo /l 0x0409 /D _VC80_UPGRADE=0x0710 %s /FO $out $in' % (q(llvm_rc), rc_inc))
    w('  description = RC [%s] $shortname' % arch)
    w('')
    w('rule link')
    w('  command = %s @$out.rsp' % q(lld_link))
    w('  rspfile = $out.rsp')
    w('  rspfile_content = $link_args $in_newline')
    w('  description = LINK [%s] $out' % arch)
    w('')

    objs = []
    pch_obj = None
    for src, pch in sources:
        if pch.lower() == 'create':
            pch_obj = os.path.join(build_dir, obj_name(src))
    if not pch_obj:
        die('no precompiled header source (PrecompiledHeader=Create) in GameDll.vcxproj')

    for src, pch in sources:
        obj = os.path.join(build_dir, obj_name(src))
        lang = '/TC' if src.lower().endswith('.c') else '/TP'
        mode = pch.lower()
        if mode == 'create':
            extra = '%s /Yc%s /Fp%s' % (lang, pch_header, q(pch_file))
            w('build %s | %s: cc %s' % (ninja_escape(obj), ninja_escape(pch_file), ninja_escape(src)))
        elif mode == 'notusing':
            extra = lang
            w('build %s: cc %s' % (ninja_escape(obj), ninja_escape(src)))
        else:
            extra = '%s /Yu%s /Fp%s' % (lang, pch_header, q(pch_file))
            w('build %s: cc %s | %s' % (ninja_escape(obj), ninja_escape(src), ninja_escape(pch_file)))
        w('  extra = ' + extra)
        w('  shortname = ' + os.path.relpath(src, CODE))
        objs.append(obj)

    for res in resources:
        out = os.path.join(build_dir, os.path.splitext(os.path.basename(res))[0] + '.res')
        w('build %s: rc %s' % (ninja_escape(out), ninja_escape(res)))
        w('  shortname = ' + os.path.relpath(res, CODE))
        objs.append(out)

    dll = os.path.join(out_dir, 'VRMod.dll')
    link_args = ['/nologo', '/DLL', '/winsysroot:' + WINSYSROOT, '/MACHINE:' + cfg['machine'],
                 '/OUT:' + dll, '/IMPLIB:' + os.path.join(out_dir, 'VRMod.lib'),
                 '/DEBUG', '/PDB:' + os.path.join(out_dir, cfg['pdb']),
                 '/INCREMENTAL:NO', '/BASE:0x39000000', '/DYNAMICBASE', '/NXCOMPAT',
                 '/MANIFEST:EMBED', "/MANIFESTUAC:level='asInvoker' uiAccess='false'"]
    link_args += cfg['ldflags']
    link_args += ['/LIBPATH:' + os.path.join(CODE, d) for d in cfg['libdirs']]
    link_args += LIBS + cfg['libs'] + DEFAULT_LIBS
    w('build %s: link %s' % (ninja_escape(dll), ' '.join(ninja_escape(o) for o in objs)))
    w('  link_args = ' + ' '.join(rsp_quote(a).replace('$', '$$') for a in link_args))
    w('default ' + ninja_escape(dll))

    ninja_file = os.path.join(build_dir, 'build.ninja')
    with open(ninja_file, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return ninja_file, dll


def check_shaders():
    shader_dir = os.path.join(CODE, 'Shaders')
    for hlsl in glob.glob(os.path.join(shader_dir, '*.hlsl')):
        name = os.path.basename(hlsl).split('.')
        header = os.path.join(shader_dir, 'generated', 'Shader%s%s.h' % (name[0], name[1].upper()))
        if not os.path.exists(header):
            die('%s is missing and cannot be generated on Linux yet (needs fxc) - copy it from a Windows build'
                % os.path.relpath(header, REPO))
        # timestamps are meaningless after a git checkout, so only flag uncommitted shader edits
        changed = subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--', hlsl], cwd=REPO).returncode == 1
        if changed:
            print('warning: %s has uncommitted changes; the generated header is not rebuilt on Linux (needs fxc)'
                  % os.path.relpath(hlsl, REPO))


def install(arch, dest):
    """Linux equivalent of install.bat / install32.bat."""
    cfg = ARCHES[arch]
    mod = os.path.join(dest, 'Mods', 'VRMod')
    os.makedirs(os.path.join(mod, cfg['bin']), exist_ok=True)
    os.makedirs(os.path.join(dest, cfg['bin']), exist_ok=True)
    for xml in glob.glob(os.path.join(REPO, '*.xml')):
        shutil.copy2(xml, mod)
    shutil.copy2(os.path.join(REPO, cfg['bin'], 'VRMod.dll'), os.path.join(mod, cfg['bin']))
    launcher = os.path.join(REPO, cfg['bin'], 'CrysisVR.exe')
    if os.path.exists(launcher):
        shutil.copy2(launcher, os.path.join(dest, cfg['bin']))
    else:
        print('warning: %s not built (tools/xbuild/build_launcher.sh)' % os.path.relpath(launcher, REPO))
    for lib in cfg['install_script_libs']:
        shutil.copy2(os.path.join(REPO, lib), os.path.join(dest, cfg['bin']))
    shutil.copytree(os.path.join(REPO, 'Game'), os.path.join(mod, 'Game'), dirs_exist_ok=True)
    for doc in ('README.md', 'LICENSE.txt'):
        shutil.copy2(os.path.join(REPO, doc), mod)
    print('[install] %s -> %s' % (arch, dest))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--arch', choices=['x86', 'x64', 'all'], default='all')
    parser.add_argument('--jobs', '-j', type=int, default=os.cpu_count())
    parser.add_argument('--warnings', action='store_true', help='show compiler warnings (/W3)')
    parser.add_argument('--clean', action='store_true', help='remove intermediate files first')
    parser.add_argument('--keep-going', '-k', action='store_true', help='compile everything and report all failures')
    parser.add_argument('--install', default=os.environ.get('CRYSIS_INSTALL_DIR'),
                        help='copy the build into a Crysis install / package dir (like install.bat)')
    args = parser.parse_args()

    ninja = shutil.which('ninja') or os.path.expanduser('~/.local/bin/ninja')
    if not os.path.exists(ninja):
        die('ninja not found - run tools/xbuild/setup_toolchain.sh first')
    check_shaders()

    for arch in (['x86', 'x64'] if args.arch == 'all' else [args.arch]):
        if args.clean:
            shutil.rmtree(os.path.join(REPO, 'BinTemp', 'xbuild', arch), ignore_errors=True)
        ninja_file, dll = write_ninja(arch, args)
        print('[build] %s -> %s' % (arch, os.path.relpath(dll, REPO)))
        cmd = [ninja, '-f', ninja_file, '-j', str(args.jobs)]
        if args.keep_going:
            cmd += ['-k', '0']
        result = subprocess.run(cmd, cwd=CODE)
        if result.returncode != 0:
            die('%s build failed' % arch)
        if args.install:
            install(arch, args.install)


if __name__ == '__main__':
    main()
