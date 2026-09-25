#!/bin/bash
# WELS-C — 项目数据面板

cd "$(dirname "$0")/.."

BLUE='\033[1;34m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

line() { printf "%s\n" "──────────────────────────────────────────────"; }
row()  { printf "  %-24s ${GREEN}%s${NC}\n" "$1" "$2"; }

echo ""
echo -e "${BLUE}WELS-C — 项目数据${NC}"
line

# ---------- 版本 ----------
VER=$(grep -oP '版本：`\K[^`]+' README.md 2>/dev/null | head -1)
row "版本" "${VER:-未定义}"

# ---------- 编译器 ----------
C_LINES=$(find compiler/src -name "*.c" -exec cat {} + 2>/dev/null | wc -l)
H_LINES=$(find compiler/src -name "*.h" -exec cat {} + 2>/dev/null | wc -l)
row "编译器 C 行数"   "${C_LINES}"
row "编译器 H 行数"   "${H_LINES}"

# ---------- 运行时 ----------
RT_LINES=0
if [ -d runtime ]; then
    RT_LINES=$(find runtime -name "*.c" -exec cat {} + 2>/dev/null | wc -l)
fi
row "运行时 C 行数"   "${RT_LINES}"

# ---------- 标准库 ----------
STD_LINES=0
if [ -d stdlib ]; then
    STD_LINES=$(find stdlib \( -name "*.wec" -o -name "*.weh" \) -exec cat {} + 2>/dev/null | wc -l)
fi
row "标准库行数"       "${STD_LINES}"

# ---------- 测试 ----------
TEST_FILES=$(find compiler/tests -name "*.wec" 2>/dev/null | wc -l)
row "测试文件数"       "${TEST_FILES}"

# ---------- 规范 ----------
if [ -f WL-C26.md ]; then
    SPEC_LINES=$(wc -l < WL-C26.md)
    row "规范行数"     "${SPEC_LINES}"
fi

# ---------- 编译产物 ----------
if [ -f compiler/wescc ]; then
    SIZE=$(du -h compiler/wescc | cut -f1)
    row "wescc 大小"   "${SIZE}"
fi

# ---------- 目录明细 ----------
echo ""
echo -e "${YELLOW}目录明细${NC}"
line
for d in compiler runtime stdlib tools editor include; do
    [ -d "$d" ] || continue
    n=$(find "$d" -type f 2>/dev/null | wc -l)
    printf "  %-24s %5s 个文件\n" "$d/" "$n"
done

# ---------- 模块明细 ----------
echo ""
echo -e "${YELLOW}编译器模块${NC}"
line
for f in compiler/src/*.c; do
    [ -f "$f" ] || continue
    n=$(wc -l < "$f")
    printf "  %-28s %5s 行\n" "$(basename $f)" "$n"
done

# ---------- 外部依赖 ----------
echo ""
echo -e "${YELLOW}外部依赖${NC}"
line
row "标准库"      "libc（当前）"
row "第三方库"    "无"
row "编译器"      "$(gcc --version | head -1 | cut -d' ' -f1-4)"

echo ""
