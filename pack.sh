#!/usr/bin/env bash
# 打包发布: 把 dist 里两套产物打成可以直接解压进游戏目录的 zip (文件在压缩包根目录)
#
# 用法: ./pack.sh [版本号] [输出目录]
#   ./pack.sh 0.1.0            -> /tmp/fvp-launcher-v0.1.0-x86.zip, -x64.zip
#
# 产物里含 FVPLauncher.exe / fvpl_hook.dll / FVPIcon.exe / FVPBind.exe / ini.example / README.md
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
VER="${1:-0.0.0}"
OUT="${2:-/tmp}"

for arch in x86 x64; do
  src="$HERE/dist/$arch"
  [ -f "$src/FVPLauncher.exe" ] && [ -f "$src/fvpl_hook.dll" ] && [ -f "$src/FVPIcon.exe" ] \
    && [ -f "$src/FVPBind.exe" ] || {
    echo "缺少 $src 下的产物, 先跑 ./build.sh" >&2
    exit 1
  }
  stage="$OUT/.stage-fvp-launcher-$VER-$arch"
  zip="$OUT/fvp-launcher-v$VER-$arch.zip"
  rm -rf "$stage" "$zip"
  mkdir -p "$stage"
  # 只放运行需要的东西, 文件直接位于 zip 根目录 => 解压进游戏目录即可用
  cp "$src/FVPLauncher.exe" "$src/fvpl_hook.dll" "$src/FVPIcon.exe" "$src/FVPBind.exe" "$stage/"
  cp "$HERE/fvp-launcher.ini.example" "$stage/"
  cp "$HERE/README.md" "$stage/README.md"
  ( cd "$stage" && zip -q -r "$zip" . )
  rm -rf "$stage"
  echo "== $zip"
done

sha256sum "$OUT"/fvp-launcher-v"$VER"-*.zip
