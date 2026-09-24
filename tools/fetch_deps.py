#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""获取第三方依赖 yyjson。

仓库不内嵌第三方源码（体积原因），构建前由本脚本按 **固定版本 + SHA-256**
拉取到 PowerMonitor/yyjson/。已存在且校验通过时直接跳过，因此可反复执行。

用法：
    python tools/fetch_deps.py            # 缺失时下载
    python tools/fetch_deps.py --force    # 强制重新下载
    python tools/fetch_deps.py --check    # 只校验，不下载（离线自检）

仅依赖 Python 标准库。若网络受限，也可手工把 yyjson 0.4.0 的 src/yyjson.h
与 src/yyjson.c 放到 PowerMonitor/yyjson/ 下，校验通过即视为就绪。
"""

import argparse
import hashlib
import os
import sys
import urllib.request

# ---- 跨平台输出保护 ----------------------------------------------------------
# Windows 的控制台/管道默认可能是 cp1252 / GBK 等窄编码，直接 print 中文会抛
# UnicodeEncodeError（在 GitHub Actions 的 Windows runner 上必现）。
# 这里把 stdout/stderr 都切到 UTF-8，并对无法编码的字符降级为转义，保证
# 脚本在任何 locale 下都不会因为“打印一行中文”而失败。
for _stream in ("stdout", "stderr"):
    _s = getattr(sys, _stream, None)
    if _s is not None and hasattr(_s, "reconfigure"):
        try:
            _s.reconfigure(encoding="utf-8", errors="backslashreplace")
        except Exception:                              # noqa: BLE001 - 尽力而为
            pass

YYJSON_VERSION = "0.4.0"
YYJSON_BASE = "https://raw.githubusercontent.com/ibireme/yyjson/%s/src/" % YYJSON_VERSION

FILES = {
    "yyjson.h": "544a05f353e35afdd7f7e43abfb6326812b52a134e21f209088d7471a5311c44",
    "yyjson.c": "f1b3941aeff3df7666c26e87122d05bbf682a0a0cb9ea7bb663a0eb85c0a2b22",
}

MIRRORS = [
    YYJSON_BASE,
    "https://cdn.jsdelivr.net/gh/ibireme/yyjson@%s/src/" % YYJSON_VERSION,
    "https://ghproxy.net/https://raw.githubusercontent.com/ibireme/yyjson/%s/src/" % YYJSON_VERSION,
]


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(dest_dir):
    """返回 (是否就绪, 问题描述列表)。"""
    problems = []
    for name, want in FILES.items():
        path = os.path.join(dest_dir, name)
        if not os.path.exists(path):
            problems.append("%s missing" % name)
            continue
        got = sha256_of(path)
        if got != want:
            problems.append("%s checksum mismatch (want %s..., got %s...)"
                            % (name, want[:12], got[:12]))
    return (not problems), problems


def download_one(dest_dir, name):
    for base in MIRRORS:
        url = base + name
        try:
            print("  - downloading %s" % url)
            with urllib.request.urlopen(url, timeout=120) as resp:
                data = resp.read()
        except Exception as exc:                      # noqa: BLE001 - 需要逐个镜像兜底
            print("    failed: %s" % exc)
            continue
        digest = hashlib.sha256(data).hexdigest()
        if digest != FILES[name]:
            print("    checksum mismatch (%s...), trying next mirror" % digest[:12])
            continue
        tmp = os.path.join(dest_dir, name + ".tmp")
        with open(tmp, "wb") as fp:
            fp.write(data)
        os.replace(tmp, os.path.join(dest_dir, name))
        print("    ok (%d bytes)" % len(data))
        return True
    return False


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    dest = os.path.join(root, "PowerMonitor", "yyjson")

    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="忽略已有文件，强制重新下载")
    ap.add_argument("--check", action="store_true", help="只校验，不下载")
    args = ap.parse_args()

    if args.force:
        for name in FILES:
            path = os.path.join(dest, name)
            if os.path.exists(path):
                os.remove(path)

    ok, problems = verify(dest)
    if ok:
        print("yyjson %s ready at %s" % (YYJSON_VERSION, dest))
        return 0

    print("yyjson %s NOT ready: %s" % (YYJSON_VERSION, "; ".join(problems)))
    if args.check:
        return 1

    os.makedirs(dest, exist_ok=True)
    for name in FILES:
        if verify(dest)[0]:
            break
        if not download_one(dest, name):
            print("!! cannot fetch %s - check network or place it manually" % name)
            return 1

    ok, problems = verify(dest)
    if not ok:
        print("!! verification still failing: %s" % "; ".join(problems))
        return 1
    print("yyjson %s fetched" % YYJSON_VERSION)
    return 0


if __name__ == "__main__":
    sys.exit(main())
