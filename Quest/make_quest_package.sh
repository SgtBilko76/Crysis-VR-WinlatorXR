#!/usr/bin/env bash
# ============================================================================
#  Linux equivalent of make_quest_package.bat: builds VRMod.dll with the clang-cl
#  cross toolchain (tools/xbuild), assembles Quest/CrysisVR-Installer/ in the same
#  layout as the Windows script and zips it as
#  Quest/CrysisVR-Installer-Beta-<version>.zip.
#
#    Quest/make_quest_package.sh [version]
#
#  The launchers are taken from Bin32/CrysisVR.exe and Bin64/CrysisVR.exe as they are
#  (build them with tools/xbuild/build_launcher.sh if needed).
# ============================================================================
set -euo pipefail

VERSION="${1:-0.2}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
QUEST="$REPO/Quest"
PACKAGE_DIR="$QUEST/CrysisVR-Installer"
ZIP_FILE="$QUEST/CrysisVR-Installer-Beta-$VERSION.zip"

for exe in Bin32/CrysisVR.exe Bin64/CrysisVR.exe; do
  [ -f "$REPO/$exe" ] || { echo "error: $exe missing (tools/xbuild/build_launcher.sh)" >&2; exit 1; }
done

rm -rf "$PACKAGE_DIR" "$ZIP_FILE"
mkdir -p "$PACKAGE_DIR/Bin32" "$PACKAGE_DIR/Bin64"

# build (no-op when up to date) and copy everything install.bat / install32.bat would copy
python3 "$REPO/tools/xbuild/build_vrmod.py" --install "$PACKAGE_DIR"

for f in README_QUEST.md README_INSTALL.txt install.cmd crysisvr_quest_settings.cfg CrysisVR.desktop; do
  cp "$QUEST/$f" "$PACKAGE_DIR/"
done

python3 - "$PACKAGE_DIR" "$ZIP_FILE" <<'EOF'
import os
import sys
import zipfile

package_dir, zip_file = sys.argv[1], sys.argv[2]
base = os.path.dirname(package_dir)
with zipfile.ZipFile(zip_file, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(package_dir):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, base))
print('zip: %s (%d files)' % (zip_file, sum(len(f) for _, _, f in os.walk(package_dir))))
EOF

echo
echo "Package assembled in $PACKAGE_DIR"
echo "Release zip: $ZIP_FILE"
echo "Push it to the headset with:"
echo "  adb push \"$PACKAGE_DIR\" /sdcard/Download/CrysisVR-Setup"
echo "  adb push \"$PACKAGE_DIR/CrysisVR.desktop\" /sdcard/Download/Winlator/"
