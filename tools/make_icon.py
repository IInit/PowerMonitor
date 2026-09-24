#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成插件图标 PowerMonitor/res/power_monitor.ico。

仓库不存放二进制文件，图标由本脚本确定性重绘：
    深蓝圆角方块 + 琥珀色闪电，表达"电力 / 功耗监测"。

用法：
    python tools/make_icon.py                     # 写入 PowerMonitor/res/power_monitor.ico
    python tools/make_icon.py --preview out.png   # 额外导出一张 256px PNG 便于查看

仅依赖 Python 标准库（zlib / struct），Windows 与 Linux 均可运行。
"""

import argparse
import os
import struct
import sys
import zlib

# ---- 跨平台输出保护 ----------------------------------------------------------
# Windows 控制台/管道可能是 cp1252 / GBK 等窄编码，print 非 ASCII 内容会抛
# UnicodeEncodeError（GitHub Actions 的 Windows runner 上必现）。统一切到
# UTF-8，并对无法编码的字符降级为转义。
for _stream in ("stdout", "stderr"):
    _s = getattr(sys, _stream, None)
    if _s is not None and hasattr(_s, "reconfigure"):
        try:
            _s.reconfigure(encoding="utf-8", errors="backslashreplace")
        except Exception:                              # noqa: BLE001 - 尽力而为
            pass

# ----------------------------------------------------------------- 图形定义
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
SS = 4                      # 超采样倍数（先放大绘制再缩小，得到抗锯齿边缘）

# 圆角方块渐变（自上而下）
BG_TOP = (0x2C, 0x4C, 0x8A)
BG_BOTTOM = (0x14, 0x22, 0x40)
BOLT = (0xFF, 0xC9, 0x3C)   # 闪电
BOLT_EDGE = (0xFF, 0xE0, 0x8A)

RADIUS_RATIO = 0.235        # 圆角半径 / 边长

# 闪电多边形（归一化坐标，原点左上）
BOLT_POLY = [
    (0.575, 0.085),
    (0.245, 0.585),
    (0.452, 0.585),
    (0.375, 0.930),
    (0.760, 0.415),
    (0.545, 0.415),
]


def _blend(dst, src, alpha):
    return tuple(int(round(d + (s - d) * alpha)) for d, s in zip(dst, src))


def _inside_rounded_rect(x, y, size, radius):
    """点是否落在圆角矩形内（坐标已归一化到 [0,size)）。"""
    if x < 0 or y < 0 or x >= size or y >= size:
        return False
    cx = min(max(x, radius), size - radius)
    cy = min(max(y, radius), size - radius)
    dx = x - cx
    dy = y - cy
    if dx == 0.0 and dy == 0.0:
        return True
    return dx * dx + dy * dy <= radius * radius


def _inside_polygon(x, y, poly):
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y):
            x_cross = (xj - xi) * (y - yi) / (yj - yi) + xi
            if x < x_cross:
                inside = not inside
        j = i
    return inside


def render_rgba(size):
    """返回 size*size 的 RGBA 字节串（未压缩）。"""
    ss = size * SS
    radius = RADIUS_RATIO * ss
    poly = [(px * ss, py * ss) for px, py in BOLT_POLY]

    # 先在超采样分辨率下算出覆盖信息
    acc = [[[0, 0, 0, 0] for _ in range(size)] for _ in range(size)]

    for sy in range(ss):
        ny = (sy + 0.5) / ss
        row = acc[min(int(ny * size), size - 1)]
        t = (sy + 0.5) / ss                     # 渐变参数
        bg = _blend(BG_TOP, BG_BOTTOM, t)
        for sx in range(ss):
            nx = (sx + 0.5) / ss
            cell = row[min(int(nx * size), size - 1)]
            if not _inside_rounded_rect(sx + 0.5, sy + 0.5, ss, radius):
                continue
            color = bg
            if _inside_polygon(sx + 0.5, sy + 0.5, poly):
                color = BOLT
                # 边缘提亮：距多边形边界 1.5 个超采样像素以内
                near_edge = False
                for dx, dy in ((-1.6, 0), (1.6, 0), (0, -1.6), (0, 1.6)):
                    if not _inside_polygon(sx + 0.5 + dx, sy + 0.5 + dy, poly):
                        near_edge = True
                        break
                if near_edge:
                    color = BOLT_EDGE
            cell[0] += color[0]
            cell[1] += color[1]
            cell[2] += color[2]
            cell[3] += 255

    per = SS * SS
    out = bytearray()
    for y in range(size):
        for x in range(size):
            r, g, b, a = acc[y][x]
            if a == 0:
                out += b"\x00\x00\x00\x00"
            else:
                # 预乘 alpha 的累计值还原为直通 alpha
                n = a // 255
                out += bytes((r // n, g // n, b // n, a // per))
    return bytes(out)


# ----------------------------------------------------------------- ICO / PNG
def _bmp_entry(size, rgba):
    """ICO 内嵌的 32bpp DIB：BITMAPINFOHEADER + 自下而上的 BGRA + AND 掩码。"""
    header = struct.pack(
        "<IiiHHIIiiII",
        40,          # biSize
        size,        # biWidth
        size * 2,    # biHeight（XOR + AND 两半）
        1,           # biPlanes
        32,          # biBitCount
        0,           # biCompression = BI_RGB
        0, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            i = (y * size + x) * 4
            r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
            xor += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    and_mask = bytearray()
    for y in range(size - 1, -1, -1):
        bits = bytearray(mask_row)
        for x in range(size):
            if rgba[(y * size + x) * 4 + 3] < 128:
                bits[x // 8] |= 0x80 >> (x % 8)
        and_mask += bits
    return header + bytes(xor) + bytes(and_mask)


def png_bytes(size, rgba):
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        raw += rgba[y * size * 4:(y + 1) * size * 4]

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def build_ico(sizes, png_from=64):
    """PNG 压缩大图（>= png_from），小图用传统 DIB，兼顾体积与兼容性。"""
    images = []
    for s in sizes:
        rgba = render_rgba(s)
        images.append((s, png_bytes(s, rgba) if s >= png_from else _bmp_entry(s, rgba)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    return bytes(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(root, "PowerMonitor", "res", "power_monitor.ico"))
    ap.add_argument("--preview", default=None)
    args = ap.parse_args()

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    data = build_ico(SIZES)
    with open(args.out, "wb") as fp:
        fp.write(data)
    print("wrote %s (%d bytes, %d sizes)" % (args.out, len(data), len(SIZES)))

    if args.preview:
        png = png_bytes(256, render_rgba(256))
        with open(args.preview, "wb") as fp:
            fp.write(png)
        print("wrote %s (%d bytes)" % (args.preview, len(png)))


if __name__ == "__main__":
    sys.exit(main())
