#!/bin/bash
# package-win.sh - Windows 发布包

set -e
cd "$(dirname "$0")/.."

VER=$(grep -oP '版本：`\K[^`]+' README.md | head -1)
PKG="WELS-C-$VER-windows-x86_64"
TMP="/tmp/$PKG"

rm -rf "$TMP"
mkdir -p "$TMP"

cp README.md WL-C26.md LICENSE LICENSE-DOCS "$TMP/"

mkdir -p "$TMP/compiler"
cp compiler/wescc.exe "$TMP/compiler/"
cp compiler/Makefile "$TMP/compiler/" 2>/dev/null || true

mkdir -p "$TMP/runtime"
cp runtime/wels_rt_win.c runtime/wels_rt_win.o runtime/wels_rt.weh "$TMP/runtime/"

mkdir -p "$TMP/stdlib"
cp stdlib/*.weh "$TMP/stdlib/"

mkdir -p "$TMP/tools"
cp tools/welsc.bat "$TMP/tools/"

mkdir -p "$TMP/examples"
cp examples/*.wec "$TMP/examples/" 2>/dev/null || true

cd /tmp
tar -czf "$PKG.tar.gz" "$PKG"
rm -rf "$TMP"

echo "打包完成: /tmp/$PKG.tar.gz"
ls -lh "/tmp/$PKG.tar.gz"
