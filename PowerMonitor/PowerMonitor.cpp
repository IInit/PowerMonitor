// PowerMonitor.cpp : 插件主类实现
#include "pch.h"
#include "PowerMonitor.h"
#include "PowerMonitorItem.h"
#include "Fields.h"
#include "Encoding.h"
#include "FileUtil.h"
#include "OptionsDlg.h"
#include "TariffDlg.h"
#include "StatsDlg.h"
#include "resource.h"
#include <shellapi.h>
#include <cstdio>

// ------------------------------------------------------------------ 项目标识
// 单一来源：所有对外署名 / 入口链接都从这里取，避免各处硬编码不一致。
static const wchar_t* const kProductName = L"PowerMonitor 功耗监测";
static const wchar_t* const kAuthor = L"init";
static const wchar_t* const kHomepage = L"https://github.com/IInit/PowerMonitor";
static const wchar_t* const kVersion = L"1.0.0";
static const wchar_t* const kLicense = L"MIT License (c) 2026 init";

// 右键命令索引
enum
{
    CMD_STATS = 0,
    CMD_TARIFF = 1,
    CMD_RESET = 2,
    CMD_ABOUT = 3,
    CMD_HOMEPAGE = 4,   // 项目主页（GitHub）
    CMD_COUNT
};

CPowerMonitor::CPowerMonitor()
{
}

CPowerMonitor::~CPowerMonitor()
{
    // 主程序未提供退出回调，DLL 卸载时兜底落盘
    if (m_inited)
        persistState();
}

// ----------------------------------------------------------------- 时间
double CPowerMonitor::nowEpoch()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULONGLONG v = (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return double(v - 116444736000000000ULL) / 10000000.0;
}

double CPowerMonitor::localTimeContext(int& hour, int& month, std::string& today) const
{
    SYSTEMTIME lt;
    ::GetLocalTime(&lt);
    hour = lt.wHour;
    month = lt.wMonth;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt.wYear, lt.wMonth, lt.wDay);
    today = buf;
    return nowEpoch();
}

// 兜底配置目录：插件 DLL 自身所在目录
static std::wstring pluginOwnDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    HMODULE hMod = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)&pluginOwnDir, &hMod))
        return std::wstring();
    if (::GetModuleFileNameW(hMod, buf, MAX_PATH) == 0)
        return std::wstring();
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        p.resize(pos);
    return p;
}

// 递归确保目录存在。
// 注意 CreateDirectoryW 只创建最后一级，父目录缺失时会失败——
// 而配置写盘失败在旧版本里是静默的（表现为"改了没生效"），所以必须先把目录补全。
static bool ensureDirExists(const std::wstring& dir)
{
    if (dir.empty())
        return false;
    DWORD attr = ::GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES)
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos && pos > 0)
    {
        std::wstring parent = dir.substr(0, pos);
        if (!(parent.size() == 2 && parent[1] == L':'))   // 跳过 "D:" 这类卷标
            ensureDirExists(parent);
    }
    return ::CreateDirectoryW(dir.c_str(), nullptr) != FALSE ||
           ::GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ----------------------------------------------------------------- 初始化
void CPowerMonitor::initialize()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    // 配置目录：优先取主程序通过 OnExtenedInfo(EI_CONFIG_DIR) 传入的路径
    if (m_config_dir.empty() && m_app != nullptr)
    {
        const wchar_t* p = m_app->GetPluginConfigDir();
        if (p != nullptr)
            m_config_dir = p;
    }
    if (m_config_dir.empty())
        m_config_dir = pluginOwnDir();
    // 目录不存在会导致 Config::save 静默失败，这里兜底创建（含所有上级目录）
    ensureDirExists(m_config_dir);

    CString dir(m_config_dir.c_str());
    if (!dir.IsEmpty() && dir.Right(1) != L"\\")
        dir += L"\\";
    m_config_path = CStringToUtf8(dir + L"PowerMonitor_config.json");
    m_state_path = CStringToUtf8(dir + L"PowerMonitor_state.json");

    // 配置
    m_cfg = Config::load(m_config_path);
    if (!FileUtil::exists(m_config_path))
        m_cfg.save(m_config_path);

    // 开机时刻
    PowerOnInfo poi = detectPowerOn();

    // 时区偏移（本地 - UTC）
    double tz_offset = 8 * 3600.0;
    TIME_ZONE_INFORMATION tzi{};
    DWORD tzr = ::GetTimeZoneInformation(&tzi);
    long bias = tzi.Bias;
    if (tzr == TIME_ZONE_ID_DAYLIGHT)
        bias += tzi.DaylightBias;
    tz_offset = double(-bias) * 60.0;

    // 账本恢复
    std::string state_text;
    FileUtil::readFile(m_state_path, state_text);
    double now = nowEpoch();
    m_meter.setConfig(m_cfg);
    m_meter.setTimeZoneOffset(tz_offset);
    m_meter.restore(poi.epoch, poi.source, state_text, now);

    // 采集器
    m_sensors.reset(new SensorHub(m_cfg));

    // 15 个显示项
    m_items.clear();
    for (const FieldDef& d : fieldDefs())
        m_items.emplace_back(new CPowerMonitorItem(this, d.key));

    ::QueryPerformanceFrequency(&m_qpc_freq);
    ::QueryPerformanceCounter(&m_last_qpc);
    m_have_last_qpc = true;

    m_inited = true;
    m_last_persist = now;

    // 先出一帧快照，避免首秒为空
    int hour = 0, month = 1;
    std::string today;
    localTimeContext(hour, month, today);
    m_snapshot = m_meter.snapshot(now, hour, month,
                                  m_sensors->gpuNames(), m_sensors->gpuLimits());
}

void CPowerMonitor::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data != nullptr)
    {
        m_config_dir = data;
        // 若已经初始化过，则把路径刷新到新目录
        if (m_inited && !m_config_dir.empty())
        {
            CString dir(m_config_dir.c_str());
            if (dir.Right(1) != L"\\")
                dir += L"\\";
            m_config_path = CStringToUtf8(dir + L"PowerMonitor_config.json");
            m_state_path = CStringToUtf8(dir + L"PowerMonitor_state.json");
        }
    }
}

void CPowerMonitor::OnInitialize(ITrafficMonitor* pApp)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    m_app = pApp;
    initialize();
}

// ----------------------------------------------------------------- 数据
void CPowerMonitor::DataRequired()
{
    if (!m_inited)
        initialize();

    LARGE_INTEGER cur_qpc;
    ::QueryPerformanceCounter(&cur_qpc);
    double dt = 0.0;
    if (m_have_last_qpc && m_qpc_freq.QuadPart > 0)
        dt = double(cur_qpc.QuadPart - m_last_qpc.QuadPart) / double(m_qpc_freq.QuadPart);

    Reading r = m_sensors->read();

    int hour = 0, month = 1;
    std::string today;
    double now = localTimeContext(hour, month, today);

    std::string segment = segmentOf(m_cfg, hour);

    m_meter.onSample(r, dt, today, hour, month, segment);

    m_snapshot = m_meter.snapshot(now, hour, month,
                                  m_sensors->gpuNames(), m_sensors->gpuLimits());

    m_last_qpc = cur_qpc;
    m_have_last_qpc = true;

    // 节流持久化（约每 20 秒）
    if (now - m_last_persist > 20.0)
    {
        persistState();
        m_last_persist = now;
    }
}

// ----------------------------------------------------------------- 显示项
IPluginItem* CPowerMonitor::GetItem(int index)
{
    if (!m_inited)
        initialize();
    if (index < 0 || index >= (int)m_items.size())
        return nullptr;   // 越界返回空指针，主程序据此结束枚举
    return m_items[index].get();
}

std::string CPowerMonitor::itemFullLabelUtf8(const std::string& key) const
{
    return fieldLabel(key);
}

std::string CPowerMonitor::itemShortLabelUtf8(const std::string& key) const
{
    for (const FieldDef& d : fieldDefs())
        if (d.key == key)
            return d.short_label;
    return key;
}

std::string CPowerMonitor::itemValueUtf8(const std::string& key) const
{
    return fieldValueText(key, m_snapshot, m_cfg);
}

std::string CPowerMonitor::itemSampleUtf8(const std::string& key) const
{
    return fieldSample(key, m_cfg);
}

// ----------------------------------------------------------------- 元信息
// 主程序"插件设置"里显示的插件信息（"详细信息"入口即读取这里的 TMI_URL）。
const wchar_t* CPowerMonitor::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return kProductName;
    case TMI_DESCRIPTION:
        return L"显示整机实时功率（CPU / 显卡 / 其他）、能耗与电费，支持峰谷分时电价与多维用量统计";
    case TMI_AUTHOR:
        return kAuthor;
    case TMI_COPYRIGHT:
        return kLicense;
    case TMI_VERSION:
        return kVersion;
    case TMI_URL:
        return kHomepage;
    default:
        return L"";
    }
}

void* CPowerMonitor::GetPluginIcon()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    int size = ::GetSystemMetrics(SM_CXSMICON);
    if (size <= 0)
        size = 16;
    HICON h = (HICON)::LoadImageW(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_POWERMONITOR),
                                  IMAGE_ICON, size, size, LR_SHARED);
    if (h != nullptr)
        m_h_icon = h;
    return (void*)h;
}

// ----------------------------------------------------------------- 选项
ITMPlugin::OptionReturn CPowerMonitor::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* parent = hParent != nullptr ? CWnd::FromHandle((HWND)hParent) : nullptr;
    COptionsDlg dlg(m_cfg, parent);
    if (dlg.DoModal() == IDOK)
    {
        m_cfg = dlg.config();
        std::wstring err;
        if (!applyConfigChanged(&err))
            ::MessageBoxW((HWND)hParent, err.c_str(), L"选项设置", MB_OK | MB_ICONERROR);
        return OR_OPTION_CHANGED;
    }
    return OR_OPTION_UNCHANGED;
}

// ----------------------------------------------------------------- 命令
int CPowerMonitor::GetCommandCount()
{
    return CMD_COUNT;
}

const wchar_t* CPowerMonitor::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case CMD_STATS:
        return L"用量统计…";
    case CMD_TARIFF:
        return L"电价设置…";
    case CMD_RESET:
        return L"重置本次统计";
    case CMD_ABOUT:
        return L"关于…";
    case CMD_HOMEPAGE:
        return L"项目主页（GitHub）…";
    default:
        return nullptr;
    }
}

void* CPowerMonitor::GetCommandIcon(int command_index)
{
    (void)command_index;
    return nullptr;
}

int CPowerMonitor::IsCommandChecked(int command_index)
{
    (void)command_index;
    return 0;
}

void CPowerMonitor::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    (void)para;
    HWND hwnd = (HWND)hWnd;
    CWnd* parent = hwnd != nullptr ? CWnd::FromHandle(hwnd) : nullptr;

    switch (command_index)
    {
    case CMD_STATS:
    {
        CStatsDlg dlg(m_meter, parent);
        dlg.DoModal();
        break;
    }
    case CMD_TARIFF:
    {
        CTariffDlg dlg(m_cfg, parent);
        // 「应用」按钮：立即落盘并把结果回显在对话框里，用户不必猜有没有保存成功
        dlg.setApplyHandler([this](const Config& c, CString& msg) -> bool {
            m_cfg = c;
            std::wstring err;
            bool ok = applyConfigChanged(&err);
            if (ok)
                msg = Utf8ToCString(m_config_path);
            else
                msg = err.empty() ? CString(L"配置未能写入磁盘") : CString(err.c_str());
            return ok;
        });
        if (dlg.DoModal() == IDOK)
        {
            m_cfg = dlg.config();
            std::wstring err;
            if (!applyConfigChanged(&err))
                ::MessageBoxW(hwnd, err.c_str(), L"电价设置", MB_OK | MB_ICONERROR);
        }
        break;
    }
    case CMD_RESET:
    {
        if (::MessageBoxW(hwnd,
                          L"确认清空本次开机的能耗、电费与峰值记录？",
                          L"重置本次统计", MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            resetCurrentSession();
        }
        break;
    }
    case CMD_ABOUT:
    {
        CString text;
        text.Format(L"%s —— TrafficMonitor 插件 v%s\r\n"
                    L"作者：%s\r\n"
                    L"许可：%s\r\n"
                    L"项目主页：%s\r\n\r\n"
                    L"· CPU 功耗：PDH 负载 + 功耗模型估算\r\n"
                    L"· 显卡功耗：NVIDIA NVML 实测\r\n"
                    L"· 其他功耗：主板 / 内存 / 硬盘等经验常量\r\n"
                    L"· 居民阶梯峰谷电价、多维用量账本\r\n\r\n"
                    L"是否现在打开项目主页？",
                    kProductName, kVersion, kAuthor, kLicense, kHomepage);
        if (::MessageBoxW(hwnd, text, L"关于", MB_YESNO | MB_ICONINFORMATION) == IDYES)
        {
            ::ShellExecuteW(hwnd, L"open", kHomepage, nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    }
    case CMD_HOMEPAGE:
    {
        // 直接打开项目主页（GitHub）
        HINSTANCE r = ::ShellExecuteW(hwnd, L"open", kHomepage, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32)
        {
            CString msg;
            msg.Format(L"未能打开浏览器，请手动访问：\r\n%s", kHomepage);
            ::MessageBoxW(hwnd, msg, L"项目主页", MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    }
}

// ----------------------------------------------------------------- Tooltip
const wchar_t* CPowerMonitor::GetTooltipInfo()
{
    const Snapshot& s = m_snapshot;
    const std::string& cur = m_cfg.currency;
    std::string utf8 = "整机功耗监控\n";
    char line[160];

    std::snprintf(line, sizeof(line),
                  "开机 %s · 本次 %.2f kWh\n",
                  shortDuration(s.power_on_seconds).c_str(), s.session_wh / 1000.0);
    utf8 += line;

    std::snprintf(line, sizeof(line),
                  "当前 %.0f W（CPU %.0f · 显卡 %.0f · 其他 %.0f）\n",
                  s.current_w, s.cpu_w, s.gpu_w, s.base_w);
    utf8 += line;

    std::snprintf(line, sizeof(line),
                  "今日 %.2f kWh · %s%.2f",
                  s.today_wh / 1000.0, cur.c_str(), s.today_cost);
    utf8 += line;

    m_tooltip_cache = Utf8ToWString(utf8);
    return m_tooltip_cache.c_str();
}

// ----------------------------------------------------------------- 配置应用 / 持久化
bool CPowerMonitor::applyConfigChanged(std::wstring* err)
{
    // 配置目录不存在会让写盘静默失败，这里兜底创建
    ensureDirExists(m_config_dir);

    bool ok = m_cfg.save(m_config_path);
    m_meter.setConfig(m_cfg);
    if (m_sensors)
        m_sensors->reload(m_cfg);
    persistState();

    if (!ok && err != nullptr)
    {
        CString path = Utf8ToCString(m_config_path);
        CString msg;
        msg.Format(L"配置未能写入磁盘：\r\n%s\r\n\r\n请确认该目录存在且可写。", path.GetString());
        *err = (LPCWSTR)msg;
    }
    return ok;
}

void CPowerMonitor::persistState()
{
    if (!m_inited)
        return;
    std::string json = m_meter.toJson(nowEpoch());
    FileUtil::writeFileAtomic(m_state_path, json);
}

void CPowerMonitor::resetCurrentSession()
{
    m_meter.resetSession(nowEpoch());
    m_have_last_qpc = false;
    ::QueryPerformanceCounter(&m_last_qpc);
    m_have_last_qpc = true;
    persistState();
}

// ----------------------------------------------------------------- 导出入口
static CPowerMonitor g_powerMonitorPlugin;

extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &g_powerMonitorPlugin;
}
