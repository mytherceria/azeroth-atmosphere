#!/bin/bash
# Builds every release file into dist/: IndoorRain.dll, the rain textures' patch-R.MPQ, the player's zip (laid out
# like the game folder, so it unzips straight into it), the Indoor Weather addon zip a launcher row installs, and
# SHA256SUMS. Usage: STORMLIB=/path/to/libstorm.so tools/make_release.sh <version>
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:?the pack version, e.g. 0.1.0}"
: "${STORMLIB:?set STORMLIB to the path of the StormLib libstorm.so}"
export STORMLIB
IRV="$(sed -n 's/^## Version: //p' IndoorRain/addon/IndoorRain/IndoorRain.toc | tr -d '\r')"
rm -rf dist && mkdir -p dist/stage dist/pack

# zip a folder's contents with sorted entries and a fixed date, so the same files always give the same zip
zipdir() {  # <folder> <zip> [<entry inside the folder>]
  python3 - "$@" <<'PY'
import os, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
start = os.path.join(root, sys.argv[3]) if len(sys.argv) > 3 else root
files = []
for d, _, fs in os.walk(start):
    for f in fs:
        files.append(os.path.relpath(os.path.join(d, f), root))
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel in sorted(files):
        info = zipfile.ZipInfo(rel.replace(os.sep, "/"), date_time=(2026, 1, 1, 0, 0, 0))
        info.external_attr = 0o644 << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, open(os.path.join(root, rel), "rb").read())
PY
}

# The DLL. build.sh runs in dll/, and ld takes the image base from the output path as written, so this path is
# part of the build: keep it as it is for the same bytes.
(cd IndoorRain/dll && ./build.sh ../../dist/IndoorRain.dll)
imports="$(i686-w64-mingw32-objdump -p dist/IndoorRain.dll | sed -n 's/.*DLL Name: //p' | sort | tr '\n' ' ')"
[ "$imports" = "KERNEL32.dll USER32.dll " ] || { echo "unexpected imports: $imports"; exit 1; }

# The textures, drawn by the scripts, then packed.
python3 RainTextures/make_rain_drop.py dist/stage/RainDrop01.blp
python3 RainTextures/make_rain_splash.py dist/stage/RainDropSplash01.blp
python3 RainTextures/make_patch_mpq.py dist/patch-R.MPQ dist/stage/RainDrop01.blp dist/stage/RainDropSplash01.blp

# The player's zip, laid out like the game folder.
P=dist/pack
mkdir -p "$P/Interface/AddOns/AtmosphereDirector" "$P/Data"
cp dist/IndoorRain.dll "$P/"
cp -r IndoorRain/addon/IndoorRain "$P/Interface/AddOns/"
cp AtmosphereDirector/AtmosphereDirector.toc AtmosphereDirector/AtmosphereDirector.lua AtmosphereDirector/Zones.lua \
   "$P/Interface/AddOns/AtmosphereDirector/"
cp -r CleanScreen "$P/Interface/AddOns/"
cp dist/patch-R.MPQ "$P/Data/"
# Never any audio: the game's sounds are read from the player's own client at run time.
if find "$P" -type f | grep -qiE '\.(ogg|wav|mp3|flac)$'; then echo "audio found in the pack, stopping"; exit 1; fi
zipdir "$P" "dist/AzerothAtmosphere-$VERSION.zip"
zipdir "$P/Interface/AddOns" "dist/IndoorRain-addon-$IRV.zip" IndoorRain

(cd dist && sha256sum "AzerothAtmosphere-$VERSION.zip" IndoorRain.dll "IndoorRain-addon-$IRV.zip" patch-R.MPQ > SHA256SUMS)
cat dist/SHA256SUMS
ls -l dist/*.zip dist/*.dll dist/*.MPQ
