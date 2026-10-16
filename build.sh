#!/usr/bin/env bash
# build.sh — FVP 启动器构建: host 单测 + x86/x64 两套 Windows 产物
# 用法: ./build.sh [游戏exe路径]   (默认拿 D:\soft\Sakura moyu\Sakura.exe 做真机 PE 断言)
set -euo pipefail
cd "$(dirname "$0")"

EXE="${1:-/mnt/d/soft/Sakura moyu/Sakura.exe}"
LOG=/tmp/fvpl-build.log
DIST=dist

PURE="src/pathutil.c src/utf8w.c src/config.c src/peinfo.c"
HOOK="src/dllmain.c src/core.c src/iat.c src/hook_io.c src/hook_nls.c src/hook_proc.c src/winutil.c"
COMMON_FLAGS="-O2 -std=c11 -Wall -Wextra -Werror -fno-strict-aliasing -fshort-wchar"

echo "== 1/3 host 纯逻辑单测 =="
gcc $COMMON_FLAGS -o /tmp/fvpl_test tests/test_all.c $PURE >"$LOG" 2>&1
cat "$LOG"
if [ -f "$EXE" ]; then
    /tmp/fvpl_test "$EXE" | tee -a "$LOG"
else
    /tmp/fvpl_test | tee -a "$LOG"
    echo "   (找不到 $EXE, PE 断言被跳过)"
fi
grep -q "fails=0" "$LOG" || { echo "单测未通过"; exit 1; }

build_pair() {   # $1=arch(-m32/-m64) $2=编译器 $3=输出目录
    local cc=$1 out=$2
    echo "== $out =="
    rm -rf "$DIST/$out"
    mkdir -p "$DIST/$out"
    "$cc-gcc" $COMMON_FLAGS -mwindows -static \
        -o "$DIST/$out/FVPLauncher.exe" src/launcher.c src/config.c src/pathutil.c \
        src/peinfo.c src/winutil.c src/utf8w.c -luser32 2>&1 | tee "$LOG"
    test -f "$DIST/$out/FVPLauncher.exe"
    "$cc-gcc" $COMMON_FLAGS -shared -static \
        -o "$DIST/$out/fvpl_hook.dll" $HOOK src/config.c src/pathutil.c src/peinfo.c \
        src/utf8w.c -luser32 -lgdi32 -lshell32 -lole32 -luuid 2>&1 | tee -a "$LOG"
    test -f "$DIST/$out/fvpl_hook.dll"
    ls -l "$DIST/$out"
}

echo
echo "== 2/3 x86 =="
build_pair i686-w64-mingw32 x86
echo
echo "== 3/3 x64 =="
build_pair x86_64-w64-mingw32 x64

echo
echo "BUILD OK"
