// Sensors.cpp : Windows 功耗数据采集实现
#include "pch.h"
#include "Sensors.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <ctime>

#pragma comment(lib, "pdh.lib")

namespace
{

// --------------------------------------------------------------- PDH
class ProcessorUtilityCounter
{
public:
    ProcessorUtilityCounter()
    {
        if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS)
            return;
        const wchar_t* path = L"\\Processor Information(_Total)\\% Processor Utility";
        PDH_STATUS st = PdhAddEnglishCounterW(m_query, path, 0, &m_counter);
        if (st != ERROR_SUCCESS)
            st = PdhAddCounterW(m_query, path, 0, &m_counter);
        if (st != ERROR_SUCCESS)
        {
            PdhCloseQuery(m_query);
            m_query = nullptr;
            return;
        }
        // 首次采集建立基线
        PdhCollectQueryData(m_query);
        m_ok = true;
    }
    ~ProcessorUtilityCounter()
    {
        if (m_query)
            PdhCloseQuery(m_query);
    }
    bool available() const { return m_ok; }

    double read()
    {
        if (!m_ok)
            return -1.0;
        if (PdhCollectQueryData(m_query) != ERROR_SUCCESS)
            return -1.0;
        PDH_FMT_COUNTERVALUE val{};
        DWORD fmt = PDH_FMT_DOUBLE | PDH_FMT_NOSCALE | PDH_FMT_NOCAP100;
        if (PdhGetFormattedCounterValue(m_counter, fmt, nullptr, &val) != ERROR_SUCCESS)
            return -1.0;
        return val.doubleValue;
    }

private:
    PDH_HQUERY m_query = nullptr;
    PDH_HCOUNTER m_counter = nullptr;
    bool m_ok = false;
};

// --------------------------------------------------------------- GetSystemTimes 回退
class SystemCpuLoad
{
public:
    SystemCpuLoad() { read(); } // 预热建立基线

    double read()
    {
        FILETIME idle{}, kernel{}, user{};
        if (!GetSystemTimes(&idle, &kernel, &user))
            return -1.0;
        ULONGLONG i = pack(idle), k = pack(kernel), u = pack(user);
        ULONGLONG cur[3] = { i, k, u };
        if (!m_have)
        {
            for (int t = 0; t < 3; ++t)
                m_last[t] = cur[t];
            m_have = true;
            return -1.0;
        }
        double d_idle = double(cur[0] - m_last[0]);
        double d_kernel = double(cur[1] - m_last[1]);
        double d_user = double(cur[2] - m_last[2]);
        for (int t = 0; t < 3; ++t)
            m_last[t] = cur[t];
        double total = d_kernel + d_user;
        if (total <= 0)
            return -1.0;
        double busy = total - d_idle;
        double util = busy / total * 100.0;
        if (util < 0) util = 0;
        if (util > 100) util = 100;
        return util;
    }

private:
    static ULONGLONG pack(const FILETIME& f)
    {
        return (ULONGLONG(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    }
    ULONGLONG m_last[3] = { 0,0,0 };
    bool m_have = false;
};

// --------------------------------------------------------------- NVML
typedef int nvmlReturn_t;
typedef void* nvmlDevice_t;
typedef nvmlReturn_t(*PFN_Init_v2)(void);
typedef nvmlReturn_t(*PFN_Shutdown)(void);
typedef nvmlReturn_t(*PFN_GetCount_v2)(unsigned int*);
typedef nvmlReturn_t(*PFN_GetHandle)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t(*PFN_GetPowerUsage)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t(*PFN_GetLimit)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t(*PFN_GetName)(nvmlDevice_t, char*, unsigned int);

class NvidiaGpu
{
public:
    NvidiaGpu()
    {
        m_dll = LoadLibraryW(L"nvml.dll");
        if (!m_dll)
            return;
        auto init = (PFN_Init_v2)GetProcAddress(m_dll, "nvmlInit_v2");
        m_shutdown = (PFN_Shutdown)GetProcAddress(m_dll, "nvmlShutdown");
        auto get_count = (PFN_GetCount_v2)GetProcAddress(m_dll, "nvmlDeviceGetCount_v2");
        auto get_handle = (PFN_GetHandle)GetProcAddress(m_dll, "nvmlDeviceGetHandleByIndex_v2");
        m_get_usage = (PFN_GetPowerUsage)GetProcAddress(m_dll, "nvmlDeviceGetPowerUsage");
        auto get_limit = (PFN_GetLimit)GetProcAddress(m_dll, "nvmlDeviceGetPowerManagementLimit");
        auto get_name = (PFN_GetName)GetProcAddress(m_dll, "nvmlDeviceGetName");

        if (!init || !get_count || !get_handle || !m_get_usage)
            return;
        if (init() != 0)
            return;
        unsigned int count = 0;
        if (get_count(&count) != 0)
            return;
        for (unsigned int idx = 0; idx < count; ++idx)
        {
            nvmlDevice_t h = nullptr;
            if (get_handle(idx, &h) != 0)
                continue;
            // 名称（新版签名：buffer）；失败用兜底名
            std::string name = "NVIDIA GPU";
            if (get_name)
            {
                char buf[96] = { 0 };
                if (get_name(h, buf, sizeof(buf)) == 0 && buf[0])
                    name = buf;
            }
            double limit = 0.0;
            if (get_limit)
            {
                unsigned int lm = 0;
                if (get_limit(h, &lm) == 0)
                    limit = lm / 1000.0;
            }
            // 预热
            unsigned int pu = 0;
            m_get_usage(h, &pu);

            m_handles.push_back(h);
            m_names.push_back(name);
            m_limits.push_back(limit);
        }
    }
    ~NvidiaGpu()
    {
        if (m_shutdown && !m_handles.empty())
            m_shutdown();
        if (m_dll)
            FreeLibrary(m_dll);
    }

    bool available() const { return !m_handles.empty(); }

    double readTotal()
    {
        if (m_handles.empty())
            return -1.0;
        double total = 0.0;
        bool any = false;
        for (nvmlDevice_t h : m_handles)
        {
            unsigned int mw = 0;
            if (m_get_usage(h, &mw) == 0)
            {
                total += mw / 1000.0;
                any = true;
            }
        }
        return any ? total : -1.0;
    }

    const std::vector<std::string>& names() const { return m_names; }
    const std::vector<double>& limits() const { return m_limits; }

private:
    HMODULE m_dll = nullptr;
    PFN_Shutdown m_shutdown = nullptr;
    PFN_GetPowerUsage m_get_usage = nullptr;
    std::vector<nvmlDevice_t> m_handles;
    std::vector<std::string> m_names;
    std::vector<double> m_limits;
};

} // namespace

// --------------------------------------------------------------- pimpl
struct SensorHub::Impl
{
    explicit Impl(const Config& c) : cfg(c), pdh(), sys_load(), gpu() {}

    Config cfg;
    ProcessorUtilityCounter pdh;
    SystemCpuLoad sys_load;
    NvidiaGpu gpu;

    Reading read()
    {
        // GPU
        double gpu_w = gpu.readTotal();
        bool gpu_measured = gpu_w >= 0;
        if (!gpu_measured)
            gpu_w = 0.0;

        // CPU 负载
        double util = pdh.read();
        if (util < 0)
            util = sys_load.read();
        if (util < 0)
            util = 0.0;
        double normalized = util / 100.0;
        if (normalized > 1.0) normalized = 1.0;
        double fraction = std::pow(normalized, cfg.cpu_load_exponent);
        double cpu_w = cfg.cpu_idle + (cfg.cpu_ppt - cfg.cpu_idle) * fraction;

        // 固定开销
        double base_w = cfg.baseline_watts;
        if (cfg.include_monitor)
            base_w += cfg.monitor_watts;

        // 校准（三分量同比缩放）
        double k = cfg.calibration != 0.0 ? cfg.calibration : 1.0;
        cpu_w *= k;
        gpu_w *= k;
        base_w *= k;

        Reading r;
        r.wall_ts = (double)std::time(nullptr);
        r.cpu_w = cpu_w;
        r.gpu_w = gpu_w;
        r.base_w = base_w;
        r.total_w = cpu_w + gpu_w + base_w;
        r.cpu_util = util;
        r.cpu_estimated = true;
        r.gpu_measured = gpu_measured;
        r.gpu_count = (int)gpu.names().size();
        r.cpu_source = pdh.available() ? "PDH \xE8\xB4\x9F\xE8\xBD\xBD\xE6\xA8\xA1\xE5\x9E\x8B"   // PDH 负载模型
                                      : "\xE7\xB3\xBB\xE7\xBB\x9F\xE5\x8D\xA0\xE7\x94\xA8\xE7\x8E\x87\xE6\xA8\xA1\xE5\x9E\x8B"; // 系统占用率模型
        r.gpu_source = gpu_measured ? "nvml" : "\xE6\x97\xA0"; // 无
        return r;
    }
};

SensorHub::SensorHub(const Config& cfg) : m_impl(new Impl(cfg)) {}
SensorHub::~SensorHub() { delete m_impl; }

Reading SensorHub::read() { return m_impl->read(); }

void SensorHub::reload(const Config& cfg) { m_impl->cfg = cfg; }

const std::vector<std::string>& SensorHub::gpuNames() const { return m_impl->gpu.names(); }
const std::vector<double>& SensorHub::gpuLimits() const { return m_impl->gpu.limits(); }
