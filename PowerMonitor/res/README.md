此目录存放插件图标 power_monitor.ico。

图标是二进制文件，为保证仓库纯文本，不入库；构建前由
`python tools/make_icon.py` 确定性重绘（深蓝圆角方块 + 琥珀色闪电），
`build_x64.sh` 会在文件缺失时自动调用。
