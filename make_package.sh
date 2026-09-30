#!/bin/sh
# make_package.sh - assemble the webp.datatype install package.
#
# Usage: make_package.sh <build_dir> [output_dir] [libwebp_dir]
#   (also run by "cmake --build . --target dist")
#
# Layout (same as the usual datatype archives):
#   Install_WebPDT(.info)
#   Classes/DataTypes/webp.datatype
#   Devs/DataTypes/WebP(.info)
#   webpdtinfo              test tool
#   WebPDT.readme (Aminet readme), LICENSE, libwebp-COPYING, libwebp-PATENTS
#
# Archive with:  cd <output_dir>/.. && lha a WebPDT.lha WebPDT

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:?usage: $0 <build_dir> [output_dir]}"
PKG="${2:-$BUILD/dist/WebPDT}"
WEBP="${3:-$SCRIPT_DIR/libwebp-main}"

for f in "$BUILD/webp.datatype" "$BUILD/WebP" "$BUILD/webpdtinfo"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: missing $f - build first." >&2
        exit 1
    fi
done

rm -rf "$PKG"
mkdir -p "$PKG/Classes/DataTypes" "$PKG/Devs/DataTypes"

cp "$BUILD/webp.datatype" "$PKG/Classes/DataTypes/webp.datatype"
cp "$BUILD/WebP"          "$PKG/Devs/DataTypes/WebP"
cp "$BUILD/webpdtinfo"    "$PKG/webpdtinfo"

cp "$SCRIPT_DIR/dist/Install_WebPDT" "$PKG/Install_WebPDT"

# Project icons (tools/mkicon.py). Double-clicking them runs the default tool:
#   Install_WebPDT.info      -> C:Installer on the script
#   Devs/DataTypes/WebP.info -> C:AddDataTypes on the descriptor (registers it)
# Icons put in dist/ by hand take precedence over the generated ones.
if [ -f "$SCRIPT_DIR/dist/Install_WebPDT.info" ]; then
    cp "$SCRIPT_DIR/dist/Install_WebPDT.info" "$PKG/"
else
    python3 "$SCRIPT_DIR/tools/mkicon.py" -o "$PKG/Install_WebPDT.info" \
        --tool C:Installer --stack 20000 --label INST \
        --tooltype APPNAME=webp.datatype --tooltype MINUSER=AVERAGE
fi
if [ -f "$SCRIPT_DIR/dist/WebP.info" ]; then
    cp "$SCRIPT_DIR/dist/WebP.info" "$PKG/Devs/DataTypes/"
else
    python3 "$SCRIPT_DIR/tools/mkicon.py" -o "$PKG/Devs/DataTypes/WebP.info" \
        --tool C:AddDataTypes --stack 4096 --label WEBP
fi

cp "$SCRIPT_DIR/dist/WebPDT.readme"        "$PKG/WebPDT.readme"
cp "$SCRIPT_DIR/LICENSE"                   "$PKG/LICENSE"
cp "$WEBP/COPYING"                        "$PKG/libwebp-COPYING"
cp "$WEBP/PATENTS"                        "$PKG/libwebp-PATENTS"

echo "Package ready in: $PKG"
find "$PKG" -type f | sort | sed "s|$PKG/||"
