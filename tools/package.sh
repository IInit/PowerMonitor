#!/usr/bin/env bash
# 打包发布产物：dist/PowerMonitor-<版本>-x64.zip
#
# 内容：PowerMonitor.dll（插件本体）+ 插件图标 + README + LICENSE
# 前置：已执行 bash build_x64.sh
set -eu

# 统一编码环境（脚本内含中文提示，Windows 窄编码控制台下会乱码）
export PYTHONIOENCODING="utf-8:backslashreplace"
export PYTHONUTF8=1
export LC_ALL="${LC_ALL:-C.UTF-8}"
export LANG="${LANG:-C.UTF-8}"

VERSION="${1:-v1.0.0}"

_here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -W 2>/dev/null || echo "$_here")"
DLL="$ROOT/x64/Release/PowerMonitor.dll"

if [ ! -f "$DLL" ]; then
    echo "!! 未找到 $DLL，请先执行 bash build_x64.sh"
    exit 1
fi

if [ ! -f "$ROOT/PowerMonitor/res/power_monitor.ico" ]; then
    python "$ROOT/tools/make_icon.py"
fi

NAME="PowerMonitor-$VERSION-x64"
STAGE="$ROOT/dist/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"

cp "$DLL" "$STAGE/"
cp "$ROOT/PowerMonitor/res/power_monitor.ico" "$STAGE/"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$STAGE/"

# 计算校验值，随包提供
( cd "$STAGE" && sha256sum PowerMonitor.dll > PowerMonitor.dll.sha256 )

# 打 zip：优先用 7z / zip，都没有时退回 PowerShell
rm -f "$ROOT/dist/$NAME.zip"
if command -v 7z >/dev/null 2>&1; then
    ( cd "$ROOT/dist" && 7z a -tzip "$NAME.zip" "$NAME" >/dev/null )
elif command -v zip >/dev/null 2>&1; then
    ( cd "$ROOT/dist" && zip -qr "$NAME.zip" "$NAME" )
else
    powershell -NoProfile -Command "Compress-Archive -Path '$STAGE/*' -DestinationPath '$ROOT/dist/$NAME.zip' -Force"
fi

echo "=== dist ==="
ls -la "$ROOT/dist"
echo "SHA-256: $(cat "$STAGE/PowerMonitor.dll.sha256")"
