## PowerMonitor v1.0.0

TrafficMonitor 整机功耗监测插件 · 首个公开发布版本
A TrafficMonitor plugin that shows live PC power draw, energy and electricity cost.
Author: **init** · <https://github.com/IInit>

### 中文 · 更新说明

**新增**

- 15 个显示项：当前功率、CPU / 显卡 / 其他功耗、每小时电费、本次开机能耗、今日 / 本月 / 累计能耗与电费、平均 / 峰值功率、开机时长、当前电价时段。
- 右键菜单 5 个命令：用量统计、电价设置、重置本次统计、关于、项目主页（GitHub）。
- 用量统计窗口：按天 / 按月 / 按年 / 每次开机 / 时段 五个标签页，底部给出合计。
- 电价设置：内置 30 个省级行政区居民阶梯电价预设，支持峰 / 平 / 谷三段、跨零点时段、丰枯季节谷价。
- 电价设置窗口新增 **「应用」** 按钮：点击即落盘，并在状态区回显 `● 已保存 hh:mm:ss → <配置文件路径>`。
- 显卡功耗读取 NVIDIA NVML 实测值；CPU 功耗为 PDH 负载 + 功耗模型估算，支持整机校准系数。
- 配置与账本以 JSON 原子写入，重启 / 异常退出不丢数据。

**修复**

- 修复电价设置窗口「确定 / 取消」被裁到客户区之外、导致用户无法保存也无法确认是否保存的问题：
  三个对话框全部改为按对话框单位（DLU）布局，客户区尺寸由模板 DLU 换算，不再硬编码像素。
- 修复 `IDD_OPTIONS` 对话框模板在资源编译时被吞掉（`resource.h` 中文注释末字节与换行拼成双字节字符，
  把下一行 `#define` 吞进注释），导致「选项设置」窗口完全打不开的问题。
- 修复电价对话框「来源信息」字段永远为空的问题（回填早于控件创建）。
- 修复配置目录不存在或为多级路径时写盘静默失败的问题，新增递归目录创建与明确的失败提示。
- 修复 `EVT_VARIANT` 取开机时刻的成员名错误（`FileTime` → `FileTimeVal`）。
- 插件宿主层重写为 TrafficMonitor 真实插件接口（API v7）：`GetItem(index)` 越界返回 `nullptr`、
  `ShowOptionsDialog` 返回 `OptionReturn`、配置目录取自 `OnExtenedInfo(EI_CONFIG_DIR)` /
  `GetPluginConfigDir()`。
- 清理虚拟对话框变量名与 Windows `dlgs.h` 宏（`grp2` / `grp3`）冲突。

**工程**

- 全新插件图标（深蓝圆角方块 + 琥珀色闪电），由 `tools/make_icon.py` 确定性重绘。
- 项目更名为 PowerMonitor，署名为 **init**，项目主页指向 <https://github.com/IInit>。
- `build_x64.sh` 一条命令完成「编译 → 链接 → 冒烟测试 → 对话框探针」，并新增 GitHub Actions 自动构建发布。
- 新增 `tools/fetch_deps.py`：按固定版本 + SHA-256 获取 yyjson 0.4.0，仓库保持纯文本。

### English · Changelog

**Added**

- 15 display items: live total power, CPU / GPU / other power, hourly cost, session energy,
  today / month / total energy and cost, average and peak power, uptime, current tariff segment.
- 5 context-menu commands: usage statistics, tariff settings, reset current session, about, homepage.
- Statistics window with five tabs — by day / month / year / boot session / hour-of-day — plus totals.
- Tariff settings with residential tiered presets for 30 provincial regions, peak / flat / valley
  segments, midnight-crossing windows and wet/dry season valley prices.
- An **Apply** button in the tariff dialog that persists immediately and echoes
  `● 已保存 hh:mm:ss → <config path>` in the status area.
- GPU power read from NVIDIA NVML (driver-measured); CPU power estimated from PDH load plus a power
  model, with a system-wide calibration multiplier.
- Config and ledger written atomically as JSON — no data loss on restart or crash.

**Fixed**

- The tariff dialog's OK / Cancel buttons used to be clipped outside the client area, so saving was
  impossible and users could not tell whether changes had been stored. All three dialogs now use
  dialog-unit (DLU) layout with the client area derived from the template, instead of hard-coded pixels.
- The `IDD_OPTIONS` template was silently swallowed at resource-compile time (a trailing byte of a
  Chinese comment merged with the newline and commented out the next `#define`), which made the
  options window impossible to open.
- The "source information" field of the tariff dialog was always empty (filled before the control existed).
- Config writes failed silently when the target directory did not exist or was nested; recursive
  directory creation and an explicit error message were added.
- Wrong `EVT_VARIANT` member when reading the boot time (`FileTime` → `FileTimeVal`).
- The plugin host layer was rewritten against the real TrafficMonitor plugin API (v7):
  `GetItem(index)` returns `nullptr` when out of range, `ShowOptionsDialog` returns `OptionReturn`,
  and the config directory comes from `OnExtenedInfo(EI_CONFIG_DIR)` / `GetPluginConfigDir()`.
- Removed virtual dialog widgets that clashed with the Windows `dlgs.h` macros (`grp2` / `grp3`).

**Engineering**

- Brand-new plugin icon (dark-blue rounded square with an amber bolt), drawn deterministically by
  `tools/make_icon.py`.
- Project renamed to PowerMonitor, authored by **init**, homepage <https://github.com/IInit>.
- `build_x64.sh` runs "compile → link → smoke test → dialog probe" in one command, and GitHub Actions
  now builds and publishes automatically.
- New `tools/fetch_deps.py` fetches yyjson 0.4.0 by pinned version + SHA-256, keeping the repo text-only.

### 安装 / Install

1. 把 `PowerMonitor.dll` 复制到 `TrafficMonitor\plugins\`（**位数需与主程序一致，本产物为 x64**）。
   Copy `PowerMonitor.dll` into `TrafficMonitor\plugins\` (x64 build — must match the host).
2. 在「选项设置 → 插件」中勾选 PowerMonitor，再在「显示设置」中勾选需要的项目。
   Tick PowerMonitor under *Options → Plugins*, then choose items under *Display settings*.

> 需要 Microsoft Visual C++ 2015-2022 运行库（动态链接 MFC）。
> Requires the Microsoft Visual C++ 2015-2022 Redistributable (dynamic MFC).

**SHA-256** 校验值见 CI 日志 / see the CI log for the SHA-256 of `PowerMonitor.dll`.
