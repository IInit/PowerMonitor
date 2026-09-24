// Meter.h : 采样积分、多维账本与快照（EnergyMeter / Snapshot）
// 跨平台纯逻辑层：时间、采集数据均由外部（插件主类）传入，不直接调用系统 API。
// 账本 JSON 序列化用 yyjson；文件读写由插件主类负责。
#pragma once

#include "Config.h"
#include <map>
#include <string>
#include <vector>

struct yyjson_doc;  // yyjson 前向声明

// 一次采样结果（对应 sensors.Reading）
struct Reading
{
    double wall_ts = 0.0;        // epoch 秒
    double total_w = 0.0;
    double cpu_w = 0.0;
    double gpu_w = 0.0;
    double base_w = 0.0;
    double cpu_util = 0.0;
    bool cpu_estimated = true;
    bool gpu_measured = false;
    int gpu_count = 0;
    std::string cpu_source;
    std::string gpu_source;
};

// 一条账目（天 / 每次开机通用）
struct LedgerEntry
{
    double wh = 0.0;
    double peak = 0.0;
    double valley = 0.0;
    double seconds = 0.0;
    double last = 0.0;           // 仅 sessions 使用：最后采样时刻
};

// 给界面用的只读快照（对应 meter.Snapshot）
struct Snapshot
{
    // 本次统计
    double session_wh = 0.0;
    double session_cost = 0.0;
    double covered_seconds = 0.0;
    double average_w = 0.0;
    double peak_w = 0.0;
    int samples = 0;
    double peak_wh = 0.0;
    double valley_wh = 0.0;
    // 当前
    double current_w = 0.0;
    double cpu_w = 0.0;
    double gpu_w = 0.0;
    double base_w = 0.0;
    double cpu_util = 0.0;
    bool in_valley = false;
    // 今日
    double today_wh = 0.0;
    double today_cost = 0.0;
    // 全局
    double total_wh = 0.0;
    int total_sessions = 0;
    // 时间
    double power_on_ts = 0.0;
    std::string power_on_source;
    double first_seen_ts = 0.0;
    double power_on_seconds = 0.0;
    // 数据源
    std::string cpu_source;
    std::string gpu_source;
    bool cpu_estimated = true;
    bool gpu_measured = false;
    std::vector<std::string> gpu_names;
    std::vector<double> gpu_limits;
    // 当前时段与电价
    std::string segment;
    double rate = 0.0;
    // 本月
    double month_wh = 0.0;
    double month_cost = 0.0;
    int month_days = 0;
    // 累计
    double total_cost = 0.0;
    int total_days = 0;
};

// 明细行（对应 stats_rows 的统一形状）
struct StatsRow
{
    std::string when;
    double wh = 0.0;
    double cost = 0.0;
    double seconds = 0.0;
    std::string note;
    std::string key;
};

class EnergyMeter
{
public:
    EnergyMeter() = default;
    explicit EnergyMeter(const Config& cfg) : m_cfg(cfg) {}

    // 从 state JSON 文本恢复账本。power_on_ts/source 来自开机时刻判定，now 为当前 epoch。
    void restore(double power_on_ts, const std::string& power_on_source,
                 const std::string& state_json, double now);

    // 每次采样记账。dt 为单调时钟测得的秒数，today 为 YYYY-MM-DD，
    // hour/month 为本地时间，segment 为 segmentOf 的结果。
    void onSample(const Reading& r, double dt, const std::string& today,
                  int hour, int month, const std::string& segment);

    void resetSession(double now);

    // 序列化为 state JSON 文本
    std::string toJson(double now) const;

    // 快照。gpu_names/gpu_limits 来自采集层（可为空）。
    Snapshot snapshot(double now, int hour, int month,
                      const std::vector<std::string>& gpu_names,
                      const std::vector<double>& gpu_limits) const;

    // 用量统计
    std::vector<StatsRow> statsRows(const std::string& kind, int month, int limit = 500) const;

    const Config& config() const { return m_cfg; }
    void setConfig(const Config& cfg) { m_cfg = cfg; }

    // 时区偏移（秒），用于把 epoch 转换为本地日期（纯日历计算，跨平台可测）。
    // 默认东八区；插件主类可按实际本地时区设置。
    void setTimeZoneOffset(double sec) { m_tz_offset = sec; }
    double timeZoneOffset() const { return m_tz_offset; }

    // 常量（账本容量与版本）
    static constexpr int HISTORY_DAYS = 400;
    static constexpr int HISTORY_SESSIONS = 240;
    static constexpr int HOUR_BUCKETS = 24;
    static constexpr int LEDGER_VERSION = 2;

private:
    Config m_cfg;

    double m_power_on_ts = 0.0;
    std::string m_power_on_source = "-";
    double m_first_seen_ts = 0.0;
    double m_session_wh = 0.0;
    double m_peak_wh = 0.0;
    double m_valley_wh = 0.0;
    double m_peak_w = 0.0;
    int m_samples = 0;
    double m_covered_seconds = 0.0;

    std::string m_today_date;
    std::map<std::string, LedgerEntry> m_days;
    std::map<long long, LedgerEntry> m_sessions;
    long long m_boot_key = 0;

    std::vector<double> m_hours = std::vector<double>(HOUR_BUCKETS, 0.0);
    std::vector<double> m_hours_valley = std::vector<double>(HOUR_BUCKETS, 0.0);

    double m_total_wh = 0.0;
    double m_total_peak_wh = 0.0;
    double m_total_valley_wh = 0.0;
    int m_total_sessions = 0;
    int m_ledger_version = 1;
    double m_tz_offset = 8 * 3600;   // 默认 UTC+8

    // 积分上一点
    double m_prev_w = 0.0;
    bool m_has_prev = false;

    Reading m_latest;
    bool m_has_latest = false;

    // ---- 恢复辅助 ----
    void migrateTotalSplitFromState(/* parsed via toJson helpers, kept simple */);
    void restoreDays(yyjson_doc* doc, double peak_ratio, double valley_ratio, int version);
    void restoreSessions(yyjson_doc* doc, double peak_ratio, double valley_ratio, int version, double now);
    void restoreHours(yyjson_doc* doc);
    void pruneDays();
    void pruneSessions();

    // ---- 记账辅助 ----
    void bumpDay(const std::string& day, double wh, double peak, double valley, double seconds);
    void bumpSession(double wall_ts, double wh, double peak, double valley, double seconds);
    void bumpHour(int hour, double wh, double valley_wh);

    // ---- 计价 / 汇总 ----
    double dayCost(const std::string& day, const LedgerEntry& e) const;
    double todayCost(const std::string& day) const;
    double sessionCost(int month) const;
    void residual(double& peak, double& valley, double& wh) const;
    double totalCost(int month) const;

    static std::string todayKey(const std::string& day) { return day; }

    // 旧账拆分比例
    void legacySplitRatio(double& peak_ratio, double& valley_ratio) const;
};

// 紧凑时长（对应 meter._duration）：45m / 2h13m / 3d4h
std::string compactDuration(double seconds);
// 短时长（对应 stripopts._short_duration）：7h21 / 1d3h
std::string shortDuration(double seconds);
// "YYYY-MM-DD" 解析出月份（1-12），失败返回 0
int monthOfDay(const std::string& day);
