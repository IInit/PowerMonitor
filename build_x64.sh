#!/usr/bin/env bash
# PowerMonitor.dll 构建脚本 (x64 / Release)
#
# 设计目标：既能在本机（VS 安装不完整、直接调用 cl.exe）跑，也能在
# CI（GitHub Actions windows runner）跑，因此工具链路径全部可用环境变量覆盖：
#
#   PM_TC       MSVC 工具集目录（含 bin/Hostx64/x64/cl.exe）
#   PM_MSVC     本机已安装的 MSVC 目录（提供 ATL 头与 CRT 库）
#   PM_SDK      Windows SDK 根目录
#   PM_SDKVER   Windows SDK 版本号
#   PM_PYTHON   Python 解释器（用于生成图标、拉取依赖）
#   PM_SKIP_DIALOG_PROBE=1   跳过对话框探针（CI 上避免依赖交互式桌面）
#
# 产物：x64/Release/PowerMonitor.dll
set -u

_here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Git Bash 的 pwd 返回 /d/... 形式，cl.exe / rc.exe 会把它当成选项；
# 优先用 pwd -W 取到 D:/... 形式。
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -W 2>/dev/null || echo "$_here")"
PROJ="$ROOT/PowerMonitor"
OUT="$ROOT/x64/Release"
OBJ="$OUT/obj"

PM_TC="${PM_TC:-D:/MyFile/PowerMonitorPlugin/.pmtoolchain/VC/Tools/MSVC/14.51.36231}"
PM_MSVC="${PM_MSVC:-D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231}"
PM_SDK="${PM_SDK:-D:/Windows Kits/10}"
PM_SDKVER="${PM_SDKVER:-10.0.26100.0}"

CL="$PM_TC/bin/Hostx64/x64/cl.exe"
LINK="$PM_TC/bin/Hostx64/x64/link.exe"
RC="$PM_SDK/bin/$PM_SDKVER/x64/rc.exe"

export MSYS2_ARG_CONV_EXCL='*'

# --------------------------------------------------------------- Python
find_python() {
    if [ -n "${PM_PYTHON:-}" ]; then echo "$PM_PYTHON"; return; fi
    for c in python3 python py; do
        if command -v "$c" >/dev/null 2>&1; then echo "$c"; return; fi
    done
    echo ""
}
PY="$(find_python)"

# --------------------------------------------------------------- 依赖与图标
# 图标不随仓库分发二进制，由脚本确定性重绘（仓库保持纯文本）。
if [ ! -f "$PROJ/res/power_monitor.ico" ]; then
    echo "=== generate icon ==="
    if [ -z "$PY" ]; then
        echo "!! 缺少 power_monitor.ico 且未找到 Python，请安装 Python 后重试"
        exit 1
    fi
    "$PY" "$ROOT/tools/make_icon.py" || exit 1
fi

# 第三方依赖 yyjson：缺失时按固定版本 + SHA-256 拉取
if [ ! -f "$PROJ/yyjson/yyjson.c" ] || [ ! -f "$PROJ/yyjson/yyjson.h" ]; then
    echo "=== fetch deps (yyjson) ==="
    if [ -z "$PY" ]; then
        echo "!! 缺少 PowerMonitor/yyjson，且未找到 Python，无法自动拉取"
        exit 1
    fi
    "$PY" "$ROOT/tools/fetch_deps.py" || exit 1
fi

mkdir -p "$OBJ"

# atlmfc/include 说明：
#   $PM_TC/atlmfc/include   —— 还原 / 安装的 MFC 头（afx*.h）
#   $PM_MSVC/atlmfc/include —— 同版本 ATL 头（__atlmfc_core.h 等），与上面互补
export INCLUDE="$PM_TC/atlmfc/include;$PM_MSVC/atlmfc/include;$PM_MSVC/include;$PM_SDK/Include/$PM_SDKVER/ucrt;$PM_SDK/Include/$PM_SDKVER/shared;$PM_SDK/Include/$PM_SDKVER/um;$PM_SDK/Include/$PM_SDKVER/winrt;$PM_SDK/Include/$PM_SDKVER/cppwinrt"
export LIB="$PM_MSVC/lib/x64;$PM_MSVC/atlmfc/lib/x64;$PM_TC/atlmfc/lib/x64;$PM_SDK/Lib/$PM_SDKVER/ucrt/x64;$PM_SDK/Lib/$PM_SDKVER/um/x64"

CFLAGS="/nologo /c /utf-8 /EHsc /std:c++17 /MD /O2 /Oi /Gy /W3 /WX- /permissive- /DNDEBUG /D_WINDOWS /D_USRDLL /D_AFXDLL /DUNICODE /D_UNICODE /I$PROJ /I$PROJ/include"

SOURCES=(
  pch.cpp
  Config.cpp
  FileUtil.cpp
  Tariffs.cpp
  Meter.cpp
  Fields.cpp
  Sensors.cpp
  PowerOn.cpp
  Encoding.cpp
  PowerMonitor.cpp
  PowerMonitorItem.cpp
  OptionsDlg.cpp
  TariffDlg.cpp
  StatsDlg.cpp
)
CSOURCES=( yyjson/yyjson.c )

fail=0
echo "=== compile C++ sources ==="
for s in "${SOURCES[@]}"; do
    o="$OBJ/$(basename "$s" .cpp).obj"
    echo "--- $s"
    "$CL" $CFLAGS "/Fo$o" "$PROJ/$s" || fail=1
done

echo "=== compile C sources ==="
for s in "${CSOURCES[@]}"; do
    o="$OBJ/$(basename "$s" .c).obj"
    echo "--- $s"
    "$CL" /nologo /c /utf-8 /MD /O2 /W3 /WX- /DNDEBUG "/Fo$o" "$PROJ/$s" || fail=1
done

echo "=== compile resources ==="
"$RC" /nologo /fo "$OBJ/PowerMonitor.res" "$PROJ/PowerMonitor.rc" || fail=1
ls -la "$OBJ/PowerMonitor.res" 2>/dev/null || fail=1

if [ "$fail" != "0" ]; then
    echo "!! compile failed, skip link"
    exit 1
fi

echo "=== link ==="
OBJS="$OBJ/pch.obj $OBJ/Config.obj $OBJ/FileUtil.obj $OBJ/Tariffs.obj $OBJ/Meter.obj $OBJ/Fields.obj $OBJ/Sensors.obj $OBJ/PowerOn.obj $OBJ/Encoding.obj $OBJ/PowerMonitor.obj $OBJ/PowerMonitorItem.obj $OBJ/OptionsDlg.obj $OBJ/TariffDlg.obj $OBJ/StatsDlg.obj $OBJ/yyjson.obj $OBJ/PowerMonitor.res"

"$LINK" /nologo /DLL /SUBSYSTEM:WINDOWS /MACHINE:X64 \
    /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE \
    "/OUT:$OUT/PowerMonitor.dll" /IMPLIB:"$OUT/PowerMonitor.lib" \
    $OBJS \
    mfc140u.lib mfcs140u.lib \
    kernel32.lib user32.lib gdi32.lib comctl32.lib ole32.lib oleaut32.lib uuid.lib \
    shell32.lib advapi32.lib version.lib psapi.lib pdh.lib winmm.lib gdiplus.lib \
    || fail=1

echo "=== result ==="
ls -la "$OUT"
if [ "$fail" != "0" ]; then
    exit 1
fi

# ---------------------------------------------------------------- 冒烟测试宿主
echo "=== build smoke host ==="
"$CL" /nologo /EHsc /std:c++17 /MD /O2 /utf-8 /DUNICODE /D_UNICODE \
    "/I$PROJ" "/I$PROJ/include" \
    "$ROOT/tests/windows/smoke_host.cpp" \
    "/Fo$OBJ/" "/Fe:$OUT/PluginSmokeTest.exe" || exit 1

echo "=== run smoke test ==="
"$OUT/PluginSmokeTest.exe" "$OUT/PowerMonitor.dll" "$OUT/_smokecfg"
rc=$?
rm -rf "$OUT/_smokecfg"
if [ "$rc" != "0" ]; then
    echo "!! smoke test failed (rc=$rc)"
    exit 1
fi

# ---------------------------------------------------------------- 对话框探针
# 实际加载 DLL 打开"电价设置 / 选项设置"对话框，枚举控件并验证
#   · 所有控件（含模板按钮）可见且未被裁剪
#   · 保存有明确回显、配置确实落盘
if [ "${PM_SKIP_DIALOG_PROBE:-0}" = "1" ]; then
    echo "=== dialog probe skipped (PM_SKIP_DIALOG_PROBE=1) ==="
    echo "=== ALL OK ==="
    exit 0
fi

echo "=== build dialog probe ==="
"$CL" /nologo /EHsc /std:c++17 /MD /O2 /utf-8 /DUNICODE /D_UNICODE \
    "/I$PROJ" "/I$PROJ/include" \
    "$ROOT/tests/windows/dialog_probe.cpp" \
    "/Fo$OBJ/" "/Fe:$OUT/DlgProbe.exe" user32.lib || exit 1

echo "=== run dialog probe ==="
"$OUT/DlgProbe.exe" "$OUT/PowerMonitor.dll" "$OUT/_probecfg"
rc=$?
rm -rf "$OUT/_probecfg"
if [ "$rc" != "0" ]; then
    echo "!! dialog probe failed (rc=$rc)"
    exit 1
fi

echo "=== ALL OK ==="
exit 0
