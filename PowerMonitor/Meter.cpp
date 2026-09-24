// Meter.cpp : 采样积分、多维账本与快照实现
#include "Meter.h"
#include "Tariffs.h"
#include "yyjson/yyjson.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// ===================================================================== 日历工具
// Howard Hinnant 的 civil/ days 互转算法（确定性纯函数，跨平台）

static void civilFromDays(int z, int& y, int& m, int& d)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;                       // [0,146096]
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0,399]
    y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);// [0,365]
    int mp = (5 * doy + 2) / 153;                     // [0,11]
    d = doy - (153 * mp + 2) / 5 + 1;                // [1,31]
    m = mp < 10 ? mp + 3 : mp - 9;                    // [1,12]
    if (m <= 2)
        y += 1;
}

static int daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int mp = m > 2 ? m - 3 : m + 9;
    int doy = (153 * mp + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// epoch(UTC) + tz_offset -> 本地 年/月/日/时/分；wday_sun=0
static void civilFromEpoch(double epoch, double tz, int& y, int& m, int& d,
                           int& hour, int& minute, int& wday_sun)
{
    long long secs = (long long)std::floor(epoch + tz);
    int days = (int)(secs / 86400);
    long long rem = secs - (long long)days * 86400;
    if (rem < 0)
    {
        days -= 1;
        rem += 86400;
    }
    civilFromDays(days, y, m, d);
    hour = (int)(rem / 3600);
    minute = (int)((rem % 3600) / 60);
    // 1970-01-01 为 Thursday(4, Sunday=0)
    wday_sun = ((days + 4) % 7 + 7) % 7;
}

static int wdayFromYMD(int y, int m, int d)
{
    int days = daysFromCivil(y, m, d);
    return ((days + 4) % 7 + 7) % 7; // Sunday=0
}

// 星期约定：周一 = 0 … 周日 = 6
static int pyWday(int wday_sun)
{
    return (wday_sun + 6) % 7;
}

static const char* const WEEKDAYS[7] = { "周一","周二","周三","周四","周五","周六","周日" };

// ===================================================================== 小工具
std::string compactDuration(double seconds)
{
    int minutes = (int)(std::max)(0.0, seconds) / 60;
    int days = minutes / 1440;
    int rem = minutes % 1440;
    int hours = rem / 60;
    int mins = rem % 60;
    char buf[32];
    if (days)
    {
        std::snprintf(buf, sizeof(buf), "%dd%dh", days, hours);
    }
    else if (hours)
    {
        std::snprintf(buf, sizeof(buf), "%dh%02dm", hours, mins);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%dm", mins);
    }
    return buf;
}

std::string shortDuration(double seconds)
{
    int minutes = (int)(std::max)(0.0, seconds) / 60;
    int days = minutes / 1440;
    int rem = minutes % 1440;
    int hours = rem / 60;
    int mins = rem % 60;
    char buf[32];
    if (days)
        std::snprintf(buf, sizeof(buf), "%dd%dh", days, hours);
    else if (hours)
        std::snprintf(buf, sizeof(buf), "%dh%02d", hours, mins);
    else
        std::snprintf(buf, sizeof(buf), "%dm", mins);
    return buf;
}

int monthOfDay(const std::string& day)
{
    // YYYY-MM-DD
    if (day.size() < 7 || day[4] != '-')
        return 0;
    try
    {
        int m = std::stoi(day.substr(5, 2));
        if (m >= 1 && m <= 12)
            return m;
    }
    catch (...) {}
    return 0;
}

// ===================================================================== JSON 读取辅助
static double jnum(yyjson_val* obj, const char* key, double def = 0.0)
{
    if (!obj || !yyjson_is_obj(obj))
        return def;
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v)
        return def;
    if (yyjson_is_real(v))
        return yyjson_get_real(v);
    if (yyjson_is_int(v) || yyjson_is_uint(v))
        return (double)yyjson_get_int(v);
    return def;
}
static int jint(yyjson_val* obj, const char* key, int def = 0) { return (int)jnum(obj, key, def); }
static bool jbool(yyjson_val* obj, const char* key)
{
    yyjson_val* v = obj ? yyjson_obj_get(obj, key) : nullptr;
    return v && yyjson_is_bool(v) ? yyjson_get_bool(v) : false;
}
static std::string jstr(yyjson_val* obj, const char* key)
{
    yyjson_val* v = obj ? yyjson_obj_get(obj, key) : nullptr;
    return (v && yyjson_is_str(v)) ? std::string(yyjson_get_str(v)) : std::string();
}
static double arrNum(yyjson_val* arr, size_t idx)
{
    if (!arr || !yyjson_is_arr(arr) || idx >= yyjson_arr_size(arr))
        return 0.0;
    yyjson_val* v = yyjson_arr_get(arr, idx);
    if (yyjson_is_real(v))
        return yyjson_get_real(v);
    if (yyjson_is_int(v) || yyjson_is_uint(v))
        return (double)yyjson_get_int(v);
    return 0.0;
}

// ===================================================================== 恢复
void EnergyMeter::restore(double power_on_ts, const std::string& power_on_source,
                          const std::string& state_json, double now)
{
    m_power_on_ts = power_on_ts;
    m_power_on_source = power_on_source;

    yyjson_doc* doc = nullptr;
    if (!state_json.empty())
    {
        std::string buf = state_json;
        doc = yyjson_read_opts(&buf[0], buf.size(), 0, nullptr, nullptr);
    }
    yyjson_val* root = doc ? yyjson_doc_get_root(doc) : nullptr;
    if (!root || !yyjson_is_obj(root))
        root = nullptr;

    m_ledger_version = jint(root, "ledger_version", 1);
    m_total_wh = jnum(root, "total_wh");
    m_total_peak_wh = jnum(root, "total_peak_wh");
    m_total_valley_wh = jnum(root, "total_valley_wh");
    m_total_sessions = jint(root, "total_sessions");

    double last_power_on = jnum(root, "power_on_ts");
    bool same_session = std::fabs(last_power_on - power_on_ts) < 30.0;
    if (same_session && root)
    {
        m_first_seen_ts = jnum(root, "first_seen_ts", now);
        m_session_wh = jnum(root, "session_wh");
        m_peak_wh = jnum(root, "peak_wh");
        m_valley_wh = jnum(root, "valley_wh");
        m_peak_w = jnum(root, "peak_w");
        m_samples = jint(root, "samples");
        m_covered_seconds = jnum(root, "covered_seconds");
        // curve 在本插件中不使用；但需恢复上一帧功率以续接积分
        m_prev_w = jnum(root, "last_w");
        m_has_prev = m_prev_w > 0.0;
    }
    else
    {
        m_first_seen_ts = now;
        m_total_sessions += 1;
    }

    // 旧账全局峰谷缺失：用会话峰谷比例反推
    if (m_total_peak_wh <= 0.0 && m_total_valley_wh <= 0.0)
    {
        double peak = jnum(root, "peak_wh");
        double valley = jnum(root, "valley_wh");
        double span = peak + valley;
        if (span > 0.0 && m_total_wh > 0.0)
        {
            double scale = m_total_wh / span;
            m_total_peak_wh = peak * scale;
            m_total_valley_wh = valley * scale;
        }
    }

    double peak_ratio = 0.0, valley_ratio = 0.0;
    legacySplitRatio(peak_ratio, valley_ratio);

    if (root)
    {
        restoreDays(doc, peak_ratio, valley_ratio, m_ledger_version);
        restoreSessions(doc, peak_ratio, valley_ratio, m_ledger_version, now);
        restoreHours(doc);
    }
    else
    {
        // 完全没有账本：仍需建立当前 session 条目
        m_today_date.clear();
        m_boot_key = (long long)(power_on_ts > 0 ? power_on_ts : now);
        LedgerEntry e;
        e.last = (std::max)(m_first_seen_ts, (double)m_boot_key);
        m_sessions[m_boot_key] = e;
    }

    if (doc)
        yyjson_doc_free(doc);
}

void EnergyMeter::legacySplitRatio(double& peak_ratio, double& valley_ratio) const
{
    double span = m_total_peak_wh + m_total_valley_wh;
    if (span <= 0.0 || m_total_wh <= 0.0)
    {
        peak_ratio = valley_ratio = 0.0;
        return;
    }
    peak_ratio = m_total_peak_wh / m_total_wh;
    valley_ratio = m_total_valley_wh / m_total_wh;
}

void EnergyMeter::restoreDays(yyjson_doc* doc, double peak_ratio, double valley_ratio, int version)
{
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* days = root ? yyjson_obj_get(root, "days") : nullptr;

    if (days && yyjson_is_obj(days))
    {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(days, &iter);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)))
        {
            const char* kstr = yyjson_get_str(key);
            yyjson_val* item = yyjson_obj_iter_get_val(key);
            if (!kstr)
                continue;
            std::string dkey = kstr;
            if (dkey.size() < 10 || dkey[4] != '-')
                continue;
            dkey = dkey.substr(0, 10);

            double wh = 0, seconds = 0, peak = 0, valley = 0;
            if (yyjson_is_obj(item))
            {
                wh = jnum(item, "wh");
                seconds = jnum(item, "seconds");
                peak = jnum(item, "peak");
                valley = jnum(item, "valley");
            }
            else if (yyjson_is_arr(item) && yyjson_arr_size(item) >= 2)
            {
                wh = arrNum(item, 0);
                if (version >= 2)
                {
                    seconds = arrNum(item, 1);
                    peak = arrNum(item, 2);
                    valley = arrNum(item, 3);
                }
                else
                {
                    seconds = arrNum(item, 2);
                    peak = (std::min)((std::max)(0.0, peak_ratio), 1.0) * wh;
                    valley = (std::min)((std::max)(0.0, 1.0 - peak_ratio), valley_ratio) * wh;
                }
            }
            else
                continue;

            LedgerEntry e;
            e.wh = (std::max)(0.0, wh);
            e.peak = (std::max)(0.0, peak);
            e.valley = (std::max)(0.0, valley);
            e.seconds = (std::max)(0.0, seconds);
            m_days[dkey] = e;
        }
    }

    // 旧版单日桶 today_wh/today_date 折入 days
    if (root)
    {
        std::string legacy_date = jstr(root, "today_date");
        double legacy_wh = jnum(root, "today_wh");
        if (legacy_date.size() >= 10 && legacy_wh > 0)
        {
            legacy_date = legacy_date.substr(0, 10);
            if (m_days.find(legacy_date) == m_days.end())
            {
                LedgerEntry e;
                e.wh = legacy_wh;
                e.peak = (std::min)((std::max)(0.0, peak_ratio), 1.0) * legacy_wh;
                e.valley = (std::min)((std::max)(0.0, 1.0 - peak_ratio), valley_ratio) * legacy_wh;
                m_days[legacy_date] = e;
            }
        }
    }

    pruneDays();
}

void EnergyMeter::pruneDays()
{
    if ((int)m_days.size() <= HISTORY_DAYS)
        return;
    // 保留日期字符串最大的 N 个
    std::vector<std::string> keys;
    for (auto& kv : m_days)
        keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    std::map<std::string, LedgerEntry> kept;
    for (size_t i = keys.size() - HISTORY_DAYS; i < keys.size(); ++i)
        kept[keys[i]] = m_days[keys[i]];
    m_days.swap(kept);
}

void EnergyMeter::restoreSessions(yyjson_doc* doc, double peak_ratio, double valley_ratio,
                                  int version, double now)
{
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* sessions = root ? yyjson_obj_get(root, "sessions") : nullptr;

    if (sessions && yyjson_is_obj(sessions))
    {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(sessions, &iter);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)))
        {
            const char* kstr = yyjson_get_str(key);
            yyjson_val* item = yyjson_obj_iter_get_val(key);
            long long boot = 0;
            try { boot = (long long)std::stod(kstr ? kstr : "0"); } catch (...) { boot = 0; }
            if (boot <= 0)
                continue;

            double wh = 0, seconds = 0, last = 0, peak = 0, valley = 0;
            if (yyjson_is_obj(item))
            {
                wh = jnum(item, "wh");
                seconds = jnum(item, "seconds");
                last = jnum(item, "last");
                peak = jnum(item, "peak");
                valley = jnum(item, "valley");
            }
            else if (yyjson_is_arr(item) && yyjson_arr_size(item) >= 3)
            {
                wh = arrNum(item, 0);
                if (version >= 2)
                {
                    seconds = arrNum(item, 1);
                    last = arrNum(item, 2);
                    peak = arrNum(item, 3);
                    valley = arrNum(item, 4);
                }
                else
                {
                    seconds = arrNum(item, 2);
                    last = arrNum(item, 3);
                    peak = (std::min)((std::max)(0.0, peak_ratio), 1.0) * wh;
                    valley = (std::min)((std::max)(0.0, 1.0 - peak_ratio), valley_ratio) * wh;
                }
            }
            else
                continue;

            LedgerEntry e;
            e.wh = (std::max)(0.0, wh);
            e.peak = (std::max)(0.0, peak);
            e.valley = (std::max)(0.0, valley);
            e.seconds = (std::max)(0.0, seconds);
            e.last = last > 0 ? last : (double)boot;
            m_sessions[boot] = e;
        }
    }

    // 当前开机条目：有则接上，无则新建
    m_boot_key = (long long)(m_power_on_ts > 0 ? (m_first_seen_ts > 0 ? m_power_on_ts : m_power_on_ts)
                                              : (m_first_seen_ts > 0 ? m_first_seen_ts : now));
    auto it = m_sessions.find(m_boot_key);
    if (it == m_sessions.end())
    {
        LedgerEntry e;
        e.last = (std::max)(m_first_seen_ts, (double)m_boot_key);
        m_sessions[m_boot_key] = e;
    }
    else
    {
        it->second.last = (std::max)(it->second.last, m_first_seen_ts);
    }

    pruneSessions();
}

void EnergyMeter::pruneSessions()
{
    if ((int)m_sessions.size() <= HISTORY_SESSIONS)
        return;
    std::vector<long long> keys;
    for (auto& kv : m_sessions)
        keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    std::map<long long, LedgerEntry> kept;
    for (size_t i = keys.size() - HISTORY_SESSIONS; i < keys.size(); ++i)
        kept[keys[i]] = m_sessions[keys[i]];
    m_sessions.swap(kept);
}

void EnergyMeter::restoreHours(yyjson_doc* doc)
{
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* hours = root ? yyjson_obj_get(root, "hours") : nullptr;
    yyjson_val* hours_v = root ? yyjson_obj_get(root, "hours_valley") : nullptr;

    if (hours && yyjson_is_arr(hours) && yyjson_arr_size(hours) == HOUR_BUCKETS)
    {
        for (int i = 0; i < HOUR_BUCKETS; ++i)
            m_hours[i] = (std::max)(0.0, arrNum(hours, i));
    }
    if (hours_v && yyjson_is_arr(hours_v) && yyjson_arr_size(hours_v) == HOUR_BUCKETS)
    {
        for (int i = 0; i < HOUR_BUCKETS; ++i)
            m_hours_valley[i] = (std::max)(0.0, arrNum(hours_v, i));
    }
}

// ===================================================================== 记账
void EnergyMeter::bumpDay(const std::string& day, double wh, double peak, double valley, double seconds)
{
    LedgerEntry& e = m_days[day];
    e.wh += wh;
    e.peak += peak;
    e.valley += valley;
    e.seconds += seconds;
}

void EnergyMeter::bumpSession(double wall_ts, double wh, double peak, double valley, double seconds)
{
    auto it = m_sessions.find(m_boot_key);
    if (it == m_sessions.end())
    {
        LedgerEntry e;
        e.last = wall_ts;
        it = m_sessions.insert({ m_boot_key, e }).first;
    }
    it->second.wh += wh;
    it->second.peak += peak;
    it->second.valley += valley;
    it->second.seconds += seconds;
    it->second.last = wall_ts;
}

void EnergyMeter::bumpHour(int hour, double wh, double valley_wh)
{
    if (hour >= 0 && hour < HOUR_BUCKETS)
    {
        m_hours[hour] += wh;
        m_hours_valley[hour] += valley_wh;
    }
}

void EnergyMeter::resetSession(double now)
{
    m_first_seen_ts = now;
    m_session_wh = m_peak_wh = m_valley_wh = m_peak_w = 0.0;
    m_samples = 0;
    m_covered_seconds = 0.0;
    m_has_prev = false;
}

// ===================================================================== 采样
void EnergyMeter::onSample(const Reading& r, double dt, const std::string& today,
                           int hour, int month, const std::string& segment)
{
    (void)month;
    m_latest = r;
    m_has_latest = true;
    m_samples += 1;
    m_peak_w = (std::max)(m_peak_w, r.total_w);

    if (today != m_today_date)
        m_today_date = today;

    double interval = (std::max)(0.5, m_cfg.sample_interval);
    if (m_has_prev && dt > 0.0 && dt < interval * 5.0)
    {
        double energy_wh = (m_prev_w + r.total_w) / 2.0 * dt / 3600.0;
        bool valley = segment == "\xE8\xB0\xB7\xE6\xAE\xB5"; // 谷段
        bool peak = segment == "\xE5\xB3\xB0\xE6\xAE\xB5";   // 峰段
        double valley_wh = valley ? energy_wh : 0.0;
        double peak_wh = peak ? energy_wh : 0.0;

        m_session_wh += energy_wh;
        m_total_wh += energy_wh;
        m_covered_seconds += dt;
        if (valley)
        {
            m_valley_wh += energy_wh;
            m_total_valley_wh += energy_wh;
        }
        else if (peak)
        {
            m_peak_wh += energy_wh;
            m_total_peak_wh += energy_wh;
        }

        bumpDay(today, energy_wh, peak_wh, valley_wh, dt);
        bumpSession(r.wall_ts, energy_wh, peak_wh, valley_wh, dt);
        bumpHour(hour, energy_wh, valley_wh);
    }

    m_prev_w = r.total_w;
    m_has_prev = true;
}

// ===================================================================== 计价 / 汇总
double EnergyMeter::dayCost(const std::string& day, const LedgerEntry& e) const
{
    int m = monthOfDay(day);
    if (m == 0)
        m = 1;
    return blendCost(m_cfg, e.wh, e.peak, e.valley, m);
}

double EnergyMeter::todayCost(const std::string& day) const
{
    auto it = m_days.find(day);
    if (it == m_days.end())
        return 0.0;
    int m = monthOfDay(day);
    if (m == 0)
        m = 1;
    return blendCost(m_cfg, it->second.wh, it->second.peak, it->second.valley, m);
}

double EnergyMeter::sessionCost(int month) const
{
    return blendCost(m_cfg, m_session_wh, m_peak_wh, m_valley_wh, month);
}

void EnergyMeter::residual(double& peak, double& valley, double& wh) const
{
    double dwh = 0, dpeak = 0, dvalley = 0;
    for (auto& kv : m_days)
    {
        dwh += kv.second.wh;
        dpeak += kv.second.peak;
        dvalley += kv.second.valley;
    }
    peak = (std::max)(0.0, m_total_peak_wh - dpeak);
    valley = (std::max)(0.0, m_total_valley_wh - dvalley);
    wh = (std::max)(0.0, m_total_wh - dwh);
}

double EnergyMeter::totalCost(int month) const
{
    double cost = 0.0;
    for (auto& kv : m_days)
    {
        int dm = monthOfDay(kv.first);
        if (dm == 0)
            dm = month;
        cost += blendCost(m_cfg, kv.second.wh, kv.second.peak, kv.second.valley, dm);
    }
    double peak = 0, valley = 0, wh = 0;
    residual(peak, valley, wh);
    cost += blendCost(m_cfg, wh, peak, valley, month);
    // 单调性下限保护
    double sc = sessionCost(month);
    double tc = 0.0;
    if (!m_today_date.empty())
        tc = todayCost(m_today_date);
    return (std::max)(cost, (std::max)(sc, tc));
}

// ===================================================================== 快照
Snapshot EnergyMeter::snapshot(double now, int hour, int month,
                              const std::vector<std::string>& gpu_names,
                              const std::vector<double>& gpu_limits) const
{
    Snapshot s;
    double hours_covered = m_covered_seconds / 3600.0;
    double avg = hours_covered > 0 ? m_session_wh / hours_covered : 0.0;

    double month_wh = 0, month_cost = 0;
    int month_days = 0;
    char mprefix[8];
    std::snprintf(mprefix, sizeof(mprefix), "%04d-%02d", /*year*/ 0, month);

    // 今日 / 本月：本月前缀需要年份，用 now 推算
    int cy = 0, cm = 0, cd = 0, ch = 0, cmin = 0, cw = 0;
    civilFromEpoch(now, m_tz_offset, cy, cm, cd, ch, cmin, cw);
    char mpre[16];
    std::snprintf(mpre, sizeof(mpre), "%04d-%02d", cy, month);

    for (auto& kv : m_days)
    {
        if (kv.first.compare(0, 7, mpre) == 0)
        {
            month_wh += kv.second.wh;
            int dm = monthOfDay(kv.first);
            if (dm == 0) dm = month;
            month_cost += blendCost(m_cfg, kv.second.wh, kv.second.peak, kv.second.valley, dm);
            month_days += 1;
        }
    }

    // 今日条目
    std::string tday;
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", cy, cm, cd);
        tday = buf;
    }
    double today_wh = 0, today_cost = 0;
    auto tit = m_days.find(tday);
    if (tit != m_days.end())
    {
        today_wh = tit->second.wh;
        today_cost = blendCost(m_cfg, tit->second.wh, tit->second.peak, tit->second.valley, month);
    }

    std::string seg = segmentOf(m_cfg, hour);
    double rate = 0.0;
    rateAt(m_cfg, hour, month, seg, rate);

    s.session_wh = m_session_wh;
    s.session_cost = sessionCost(month);
    s.covered_seconds = m_covered_seconds;
    s.average_w = avg;
    s.peak_w = m_peak_w;
    s.samples = m_samples;
    s.peak_wh = m_peak_wh;
    s.valley_wh = m_valley_wh;

    s.current_w = m_has_latest ? m_latest.total_w : 0.0;
    s.cpu_w = m_has_latest ? m_latest.cpu_w : 0.0;
    s.gpu_w = m_has_latest ? m_latest.gpu_w : 0.0;
    s.base_w = m_has_latest ? m_latest.base_w : 0.0;
    s.cpu_util = m_has_latest ? m_latest.cpu_util : 0.0;
    s.in_valley = seg == "\xE8\xB0\xB7\xE6\xAE\xB5";

    s.today_wh = today_wh;
    s.today_cost = today_cost;

    s.total_wh = m_total_wh;
    s.total_sessions = m_total_sessions;

    s.power_on_ts = m_power_on_ts;
    s.power_on_source = m_power_on_source;
    s.first_seen_ts = m_first_seen_ts;
    s.power_on_seconds = (std::max)(0.0, now - m_power_on_ts);

    s.cpu_source = m_has_latest ? m_latest.cpu_source : std::string("-");
    s.gpu_source = m_has_latest ? (m_latest.gpu_source.empty() ? std::string("-") : m_latest.gpu_source) : std::string("-");
    s.cpu_estimated = true;
    s.gpu_measured = m_has_latest ? m_latest.gpu_measured : false;
    s.gpu_names = gpu_names;
    s.gpu_limits = gpu_limits;

    s.segment = seg;
    s.rate = rate;

    s.month_wh = month_wh;
    s.month_cost = month_cost;
    s.month_days = month_days;

    s.total_cost = totalCost(month);
    s.total_days = (int)m_days.size();

    (void)mprefix;
    return s;
}

// ===================================================================== 用量统计
std::vector<StatsRow> EnergyMeter::statsRows(const std::string& kind, int month, int limit) const
{
    std::vector<StatsRow> rows;

    if (kind == "session")
    {
        std::vector<long long> boots;
        for (auto& kv : m_sessions)
            boots.push_back(kv.first);
        std::sort(boots.begin(), boots.end(), std::greater<long long>());
        for (long long boot : boots)
        {
            const LedgerEntry& e = m_sessions.at(boot);
            int y = 0, m = 0, d = 0, h = 0, min = 0, wd = 0;
            civilFromEpoch((double)boot, m_tz_offset, y, m, d, h, min, wd);
            double span = (std::max)(0.0, e.last - (double)boot);
            StatsRow r;
            char when[32];
            std::snprintf(when, sizeof(when), "%02d-%02d %02d:%02d", m, d, h, min);
            r.when = when;
            r.wh = e.wh;
            r.cost = blendCost(m_cfg, e.wh, e.peak, e.valley, m);
            r.seconds = e.seconds;
            r.note = "开机 " + compactDuration(span);
            r.key = std::to_string(boot);
            rows.push_back(r);
        }
    }
    else if (kind == "hour")
    {
        double total = 0;
        for (double v : m_hours)
            total += v;
        for (int h = 0; h < HOUR_BUCKETS; ++h)
        {
            double wh = m_hours[h];
            double vwh = m_hours_valley[h];
            double share = total > 0 ? wh / total * 100.0 : 0.0;
            StatsRow r;
            char when[32];
            std::snprintf(when, sizeof(when), "%02d:00 - %02d:00", h, h + 1);
            r.when = when;
            r.wh = wh;
            r.cost = blendCost(m_cfg, wh, 0.0, vwh, month);
            r.note = wh > 0 ? "占 " + [share]() { char b[16]; std::snprintf(b, sizeof(b), "%.1f%%", share); return std::string(b); }() : "\xE2\x80\x94"; // —
            r.key = std::to_string(h);
            rows.push_back(r);
        }
    }
    else if (kind == "day")
    {
        // 每天开机次数
        std::map<std::string, int> per_day;
        for (auto& kv : m_sessions)
        {
            int y = 0, m = 0, d = 0, h = 0, min = 0, wd = 0;
            civilFromEpoch((double)kv.first, m_tz_offset, y, m, d, h, min, wd);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
            per_day[buf] += 1;
        }

        std::vector<std::string> days;
        for (auto& kv : m_days)
            days.push_back(kv.first);
        std::sort(days.begin(), days.end(), std::greater<std::string>());
        for (auto& day : days)
        {
            const LedgerEntry& e = m_days.at(day);
            int y = std::stoi(day.substr(0, 4));
            int m = std::stoi(day.substr(5, 2));
            int d = std::stoi(day.substr(8, 2));
            int wd = wdayFromYMD(y, m, d);
            int launched = per_day.count(day) ? per_day[day] : 0;

            StatsRow r;
            r.when = day + " " + WEEKDAYS[pyWday(wd)];
            r.wh = e.wh;
            r.cost = blendCost(m_cfg, e.wh, e.peak, e.valley, m);
            r.seconds = e.seconds;
            if (launched > 0)
            {
                char b[32];
                std::snprintf(b, sizeof(b), "开机 %d 次", launched);
                r.note = b;
            }
            else
                r.note = "\xE2\x80\x94"; // —
            r.key = day;
            rows.push_back(r);
        }
    }
    else if (kind == "month" || kind == "year")
    {
        int width = kind == "month" ? 7 : 4;
        struct Acc { double wh, cost, seconds, days, peak, valley; };
        std::map<std::string, Acc> groups;
        for (auto& kv : m_days)
        {
            std::string key = kv.first.substr(0, width);
            Acc& a = groups[key];
            int dm = monthOfDay(kv.first);
            a.wh += kv.second.wh;
            a.cost += blendCost(m_cfg, kv.second.wh, kv.second.peak, kv.second.valley, dm);
            a.seconds += kv.second.seconds;
            a.peak += kv.second.peak;
            a.valley += kv.second.valley;
            a.days += 1;
        }
        std::vector<std::string> keys;
        for (auto& kv : groups)
            keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end(), std::greater<std::string>());
        for (auto& key : keys)
        {
            Acc& a = groups[key];
            StatsRow r;
            r.when = key;
            r.wh = a.wh;
            r.cost = a.cost;
            r.seconds = a.seconds;
            char b[64];
            double daily = a.days > 0 ? a.wh / a.days / 1000.0 : 0.0;
            std::snprintf(b, sizeof(b), "%d 天 · 日均 %.2f kWh", (int)a.days, daily);
            r.note = b;
            r.key = key;
            rows.push_back(r);
        }
    }

    if ((int)rows.size() > limit)
        rows.resize(limit);
    return rows;
}

// ===================================================================== 序列化
std::string EnergyMeter::toJson(double now) const
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_int(doc, root, "ledger_version", LEDGER_VERSION);
    yyjson_mut_obj_add_real(doc, root, "power_on_ts", m_power_on_ts);
    yyjson_mut_obj_add_real(doc, root, "first_seen_ts", m_first_seen_ts);
    yyjson_mut_obj_add_real(doc, root, "session_wh", std::round(m_session_wh * 10000) / 10000);
    yyjson_mut_obj_add_real(doc, root, "peak_wh", std::round(m_peak_wh * 10000) / 10000);
    yyjson_mut_obj_add_real(doc, root, "valley_wh", std::round(m_valley_wh * 10000) / 10000);
    yyjson_mut_obj_add_real(doc, root, "peak_w", std::round(m_peak_w * 100) / 100);
    yyjson_mut_obj_add_int(doc, root, "samples", m_samples);
    yyjson_mut_obj_add_real(doc, root, "covered_seconds", std::round(m_covered_seconds * 100) / 100);
    yyjson_mut_obj_add_real(doc, root, "last_w", std::round(m_prev_w * 100) / 100);
    yyjson_mut_obj_add_str(doc, root, "today_date", m_today_date.c_str());

    // days
    yyjson_mut_val* days = yyjson_mut_obj(doc);
    for (auto& kv : m_days)
    {
        yyjson_mut_val* arr = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.wh * 1000) / 1000);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.seconds * 10) / 10);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.peak * 1000) / 1000);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.valley * 1000) / 1000);
        yyjson_mut_obj_add_val(doc, days, kv.first.c_str(), arr);
    }
    yyjson_mut_obj_add_val(doc, root, "days", days);

    // sessions
    yyjson_mut_val* sessions = yyjson_mut_obj(doc);
    for (auto& kv : m_sessions)
    {
        yyjson_mut_val* arr = yyjson_mut_arr(doc);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.wh * 1000) / 1000);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.seconds * 10) / 10);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.last * 10) / 10);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.peak * 1000) / 1000);
        yyjson_mut_arr_add_real(doc, arr, std::round(kv.second.valley * 1000) / 1000);
        yyjson_mut_obj_add_val(doc, sessions, std::to_string(kv.first).c_str(), arr);
    }
    yyjson_mut_obj_add_val(doc, root, "sessions", sessions);

    // hours
    yyjson_mut_val* hours = yyjson_mut_arr(doc);
    yyjson_mut_val* hours_v = yyjson_mut_arr(doc);
    for (int i = 0; i < HOUR_BUCKETS; ++i)
    {
        yyjson_mut_arr_add_real(doc, hours, std::round(m_hours[i] * 1000) / 1000);
        yyjson_mut_arr_add_real(doc, hours_v, std::round(m_hours_valley[i] * 1000) / 1000);
    }
    yyjson_mut_obj_add_val(doc, root, "hours", hours);
    yyjson_mut_obj_add_val(doc, root, "hours_valley", hours_v);

    yyjson_mut_obj_add_real(doc, root, "total_wh", std::round(m_total_wh * 10000) / 10000);
    yyjson_mut_obj_add_real(doc, root, "total_peak_wh", std::round(m_total_peak_wh * 10000) / 10000);
    yyjson_mut_obj_add_real(doc, root, "total_valley_wh", std::round(m_total_valley_wh * 10000) / 10000);
    yyjson_mut_obj_add_int(doc, root, "total_sessions", m_total_sessions);
    yyjson_mut_obj_add_real(doc, root, "saved_at", now);

    size_t out_len = 0;
    char* str = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &out_len);
    std::string result;
    if (str)
    {
        result = str;
        std::free(str);
    }
    yyjson_mut_doc_free(doc);
    return result;
}
