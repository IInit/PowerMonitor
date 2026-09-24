// Fields.cpp : 15 个显示字段的取值实现
#include "Fields.h"
#include <cstdio>

const std::vector<FieldDef>& fieldDefs()
{
    static const std::vector<FieldDef> defs = {
        { "current",    "当前功率",   "当前" },
        { "cpu",        "处理器功率", "CPU" },
        { "gpu",        "显卡功率",   "显卡" },
        { "base",       "其他功耗",   "其他" },
        { "cost",       "本次电费",   "本次" },
        { "session",    "本次电量",   "本次" },
        { "today",      "今日电量",   "今日" },
        { "today_cost", "今日电费",   "今日" },
        { "avg",        "平均功率",   "平均" },
        { "peak",       "峰值功率",   "峰值" },
        { "uptime",     "开机时长",   "开机" },
        { "segment",    "当前时段",   "时段" },
        { "month",      "本月电量",   "本月" },
        { "total",      "累计电量",   "累计" },
        { "total_cost", "累计电费",   "累计" },
    };
    return defs;
}

std::string fieldLabel(const std::string& key)
{
    for (const FieldDef& d : fieldDefs())
        if (d.key == key)
            return d.label;
    return key;
}

static std::string fmt(const char* f, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

// 电量单位选择：>=1000 Wh 用 kWh
static void energyUnit(double wh, std::string& val, std::string& unit)
{
    if (wh >= 1000.0)
    {
        val = fmt("%.2f", wh / 1000.0);
        unit = "kWh";
    }
    else
    {
        val = fmt("%.0f", wh);
        unit = "Wh";
    }
}

std::string fieldValueText(const std::string& key, const Snapshot& s, const Config& cfg)
{
    const std::string& cur = cfg.currency;

    if (key == "current")
        return fmt("%.0f W", s.current_w);
    if (key == "cpu")
        return fmt("%.0f W", s.cpu_w);
    if (key == "gpu")
    {
        if (s.gpu_names.empty())
            return "N/A";
        return fmt("%.0f W", s.gpu_w);
    }
    if (key == "base")
        return fmt("%.0f W", s.base_w);
    if (key == "cost")
        return cur + fmt("%.2f", s.session_cost);
    if (key == "session")
    {
        std::string v, u;
        energyUnit(s.session_wh, v, u);
        return v + " " + u;
    }
    if (key == "today")
        return fmt("%.2f kWh", s.today_wh / 1000.0);
    if (key == "today_cost")
        return cur + fmt("%.2f", s.today_cost);
    if (key == "avg")
        return fmt("%.0f W", s.average_w);
    if (key == "peak")
        return fmt("%.0f W", s.peak_w);
    if (key == "uptime")
        return shortDuration(s.power_on_seconds);
    if (key == "segment")
        return s.segment;
    if (key == "month")
        return fmt("%.1f kWh", s.month_wh / 1000.0);
    if (key == "total")
    {
        std::string v, u;
        energyUnit(s.total_wh, v, u);
        return v + " " + u;
    }
    if (key == "total_cost")
        return cur + fmt("%.2f", s.total_cost);
    return "";
}

std::string fieldSample(const std::string& key, const Config& cfg)
{
    const std::string& cur = cfg.currency;
    if (key == "current" || key == "cpu" || key == "gpu" || key == "base" ||
        key == "avg" || key == "peak")
        return "999 W";
    if (key == "cost" || key == "today_cost" || key == "total_cost")
        return cur + "999.99";
    if (key == "session" || key == "total")
        return "99.99 kWh";
    if (key == "today" || key == "month")
        return "99.99 kWh";
    if (key == "uptime")
        return "99d9h";
    if (key == "segment")
        return "峰段";
    return "999";
}
