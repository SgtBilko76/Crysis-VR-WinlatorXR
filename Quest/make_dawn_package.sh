#!/usr/bin/env bash
# ============================================================================
#  Builds the WinlatorXR *Dawn* package: the files that are extracted over an
#  existing Crysis installation plus a container profile Dawn can import
#  (Settings -> import profile), in the same shape as the Far Cry VR package.
#
#    Quest/make_dawn_package.sh [version]
#
#  Same payload as CrysisVR-Installer-Beta-*.zip (Bin32 + Bin64 + Mods/VRMod),
#  but without install.cmd: Dawn users unzip into D:\Crysis and import the
#  .wxrprofile.json.
# ============================================================================
set -euo pipefail

VERSION="${1:-0.3}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
QUEST="$REPO/Quest"
NAME="CrysisVR-Dawn-$VERSION"
PACKAGE_DIR="$QUEST/$NAME"
ZIP_FILE="$QUEST/$NAME.zip"
PROFILE="CrysisVR - proton-9.0-x86_64 - Quest 3.wxrprofile.json"

# build (no-op when up to date) into a staging dir laid out like the game folder
STAGING="$QUEST/.dawn-staging"
rm -rf "$STAGING" "$PACKAGE_DIR" "$ZIP_FILE"
mkdir -p "$STAGING"
python3 "$REPO/tools/xbuild/build_vrmod.py" --install "$STAGING"

# the whole payload install.cmd would copy (Bin32 + Bin64 + Mods/VRMod)
mv "$STAGING" "$PACKAGE_DIR"

cp "$QUEST/crysisvr_quest.cfg" "$PACKAGE_DIR/"
cp "$QUEST/README_DAWN.txt" "$QUEST/README_QUEST.md" "$PACKAGE_DIR/"
# the tuned settings are the whole system.cfg here (Crysis' stock file only holds a few defaults)
cp "$QUEST/crysisvr_quest_settings.cfg" "$PACKAGE_DIR/system.cfg"
cp "$QUEST/$PROFILE" "$PACKAGE_DIR/"

python3 - "$PACKAGE_DIR" "$ZIP_FILE" <<'EOF'
import os
import sys
import zipfile

package_dir, zip_file = sys.argv[1], sys.argv[2]
count = 0
with zipfile.ZipFile(zip_file, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(package_dir):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, package_dir))
            count += 1
print('zip: %s (%d files)' % (zip_file, count))
EOF

echo
echo "Package: $PACKAGE_DIR"
echo "Zip:     $ZIP_FILE"
