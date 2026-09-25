#!/bin/bash
# package.sh — 打包发布版

set -e

cd "$(dirname "$0")/.."

VER=$(grep -oP '版本：`\K[^`]+' README.md | head -1)
PLATFORM="${1:-linux-x86_64}"
PKG="WELS-C-$VER-$PLATFORM"
TMP="/tmp/$PKG"

rm -rf "$TMP"
mkdir -p "$TMP"

# 源码/规范/授权
cp README.md WL-C26.md LICENSE LICENSE-DOCS "$TMP/"

# 编译器
mkdir -p "$TMP/compiler"
cp compiler/wescc "$TMP/compiler/"
cp compiler/Makefile "$TMP/compiler/" 2>/dev/null || true

# 运行时
mkdir -p "$TMP/runtime"
cp runtime/wels_rt.o runtime/wels_rt.c runtime/wels_rt.weh runtime/Makefile "$TMP/runtime/"

# 标准库
mkdir -p "$TMP/stdlib"
cp stdlib/*.weh "$TMP/stdlib/"

# 工具
mkdir -p "$TMP/tools"
cp tools/welsc "$TMP/tools/"

# 示例
mkdir -p "$TMP/examples"
cp examples/*.wec "$TMP/examples/" 2>/dev/null || true

# 打包
cd /tmp
tar -czf "$PKG.tar.gz" "$PKG"
rm -rf "$TMP"

echo "打包完成: /tmp/$PKG.tar.gz"
ls -lh "/tmp/$PKG.tar.gz"
