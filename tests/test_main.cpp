// test_main.cpp : 跨平台核心算法测试（g++ 编译，不依赖 Windows / MFC）
#include "Config.h"
#include "FileUtil.h"
#include "Tariffs.h"
#include "Meter.h"
#include "Fields.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static bool approx(double a, double b, double eps = 1e-6)
{
    return std::fabs(a - b) <= eps;
}

#define CHECK(cond, msg) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__); } \
} while (0)

// ------------------------------------------------- 日期 / epoch helper
static int t_daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int mp = m > 2 ? m - 3 : m + 9;
    int doy = (153 * mp + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
// 本地(UTC+8) 日期时间 -> UTC epoch
static double epochLocal(int y, int m, int d, int h, int min = 0)
{
    return double(t_daysFromCivil(y, m, d) - t_daysFromCivil(1970, 1, 1)) * 86400.0
           + h * 3600 + min * 60 - 8 * 3600;
}

static Reading makeReading(double total, double cpu, double gpu, double base)
{
    Reading r;
    r.wall_ts = 1000000;
    r.total_w = total;
    r.cpu_w = cpu;
    r.gpu_w = gpu;
    r.base_w = base;
    return r;
}

// ---------------------------------------------------------------- parseHours
static void testParseHours()
{
    std::printf("== parseHours ==\n");
    {
        auto h = parseHours("8-22");
        CHECK(h.size() == 14, "8-22 -> 14 hours");
        CHECK(h.count(8) && h.count(21) && !h.count(22) && !h.count(7), "8-22 boundaries");
    }
    {
        auto h = parseHours("23-7");
        CHECK(h.size() == 8, "23-7 -> 8 hours");
        CHECK(h.count(23) && h.count(0) && h.count(6) && !h.count(7) && !h.count(22), "23-7 wrap");
    }
    {
        auto h = parseHours("11-17,20-22");
        CHECK(h.size() == 8, "two segments 6+2");
        CHECK(h.count(11) && h.count(16) && h.count(20) && h.count(21) && !h.count(17) && !h.count(22), "two seg bounds");
    }
    {
        auto h = parseHours("12");
        CHECK(h.size() == 1 && h.count(12), "single hour");
    }
    {
        auto h = parseHours("0-24");
        CHECK(h.size() == 24, "full day 0-24");
    }
    {
        // 全角逗号“，”：23，0，1
        auto h = parseHours(std::string("23\xEF\xBC\x8C") + "0\xEF\xBC\x8C" + "1");
        CHECK(h.size() == 3 && h.count(23) && h.count(0) && h.count(1), "fullwidth comma");
    }
}

// ---------------------------------------------------------------- 预设
static void testRegions()
{
    std::printf("== regions ==\n");
    const auto& rs = allRegions();
    CHECK(rs.size() == 30, "30 regions");
    bool all_ok = true;
    for (const Region& r : rs)
    {
        if (r.plans.empty()) { all_ok = false; break; }
        for (const Plan& p : r.plans)
        {
            if (p.peak < 0 || p.flat < 0 || p.valley < 0) { all_ok = false; }
        }
        parseHours(r.defaultPlan().peak_hours);
        parseHours(r.defaultPlan().valley_hours);
    }
    CHECK(all_ok, "all plans have valid prices");

    CHECK(findRegion("四川") != nullptr, "find exact");
    CHECK(findRegion("成都市") != nullptr, "find by city keyword");
    CHECK(findRegion("不存在的地方") == nullptr, "find miss");

    const Region* sc = findRegion("四川");
    CHECK(sc->plans.size() == 4, "sichuan 4 plans");
    CHECK(approx(sc->defaultPlan().peak, 0.5224), "sichuan peak");
    CHECK(approx(sc->defaultPlan().valley, 0.2535), "sichuan valley dry");
    CHECK(approx(sc->defaultPlan().resolvedValleyWet(), 0.175), "sichuan valley wet");
}

// ---------------------------------------------------------------- 计价
static void testTariff()
{
    std::printf("== tariff / segment ==\n");
    Config cfg; // 默认四川
    CHECK(segmentOf(cfg, 10) == "平段", "10:00 flat (no peak set)");
    CHECK(segmentOf(cfg, 2) == "谷段", "02:00 valley");
    CHECK(segmentOf(cfg, 23) == "谷段", "23:00 valley");
    CHECK(segmentOf(cfg, 7) == "平段", "07:00 flat");

    CHECK(approx(valleyRate(cfg, 9), 0.175), "sep wet valley 0.175");
    CHECK(approx(valleyRate(cfg, 1), 0.2535), "jan dry valley 0.2535");

    CHECK(approx(blendCost(cfg, 1000, 0, 1000, 9), 0.175), "pure valley cost");
    CHECK(approx(blendCost(cfg, 1000, 0, 0, 9), 0.5224), "pure flat cost");
    double c = blendCost(cfg, 1000, 800, 800, 9);
    CHECK(approx(c, 0.45292, 1e-6), "clamp peak/valley");

    Config zj;
    const Region* zjr = findRegion("浙江");
    applyPlan(zj, *zjr, zjr->plans[0]);
    CHECK(segmentOf(zj, 10) == "峰段", "zhejiang 10 peak");
    CHECK(segmentOf(zj, 23) == "谷段", "zhejiang 23 valley");
}

// ---------------------------------------------------------------- 积分 / 账本
static void testMeter()
{
    std::printf("== meter integration ==\n");
    Config cfg;
    EnergyMeter m(cfg);
    double T0 = epochLocal(2026, 9, 24, 0); // 开机本地0点
    m.restore(T0, "test", "", T0);

    std::string D = "2026-09-24";
    double now10 = T0 + 10 * 3600;
    m.onSample(makeReading(100, 60, 20, 20), 2.0, D, 10, 9, "平段");
    m.onSample(makeReading(120, 70, 30, 20), 2.0, D, 10, 9, "平段");

    double e1 = 220.0 / 3600.0;
    Snapshot s = m.snapshot(now10 + 4, 10, 9, {}, {});
    CHECK(approx(s.session_wh, e1), "session energy trapezoid");
    CHECK(approx(s.total_wh, e1), "total energy");
    CHECK(approx(s.today_wh, e1), "today energy");
    CHECK(approx(s.average_w, 110.0), "average 110 W");
    CHECK(approx(s.peak_w, 120.0), "peak 120 W");
    CHECK(approx(s.covered_seconds, 2.0), "covered 2s");
    CHECK(s.samples == 2, "2 samples");

    // 谷段帧 hour=2（凌晨谷），segment 直接传谷
    m.onSample(makeReading(100, 60, 20, 20), 2.0, D, 2, 9, "谷段");
    double e2 = 220.0 / 3600.0;
    s = m.snapshot(now10 + 6, 2, 9, {}, {});
    CHECK(approx(s.session_wh, e1 + e2), "session accumulates");
    CHECK(approx(s.valley_wh, e2), "valley split");
    CHECK(approx(s.peak_wh, 0.0), "no peak");
    double expect_cost = (e1 * 0.5224 + e2 * 0.175) / 1000.0;
    CHECK(approx(s.session_cost, expect_cost, 1e-9), "session cost flat+valley");

    auto hour_rows = m.statsRows("hour", 9);
    CHECK(hour_rows.size() == 24, "24 hour rows");
    CHECK(approx(hour_rows[10].wh, e1), "hour 10 bucket");
    CHECK(approx(hour_rows[2].wh, e2), "hour 2 bucket");

    // 睡眠空档 dt 很大，不积分
    double before = s.session_wh;
    m.onSample(makeReading(100, 60, 20, 20), 7200.0, D, 10, 9, "平段");
    s = m.snapshot(now10 + 7206, 10, 9, {}, {});
    CHECK(approx(s.session_wh, before), "big dt (sleep) skipped");
}

// ---------------------------------------------------------------- 跨天
static void testCrossDay()
{
    std::printf("== cross day ==\n");
    Config cfg;
    EnergyMeter m(cfg);
    double T0 = epochLocal(2026, 9, 24, 22);
    m.restore(T0, "test", "", T0);

    // 22点建 prev；23点积分到24；次日0点积分到25
    m.onSample(makeReading(100, 60, 20, 20), 2.0, "2026-09-24", 22, 9, "平段");
    m.onSample(makeReading(100, 60, 20, 20), 2.0, "2026-09-24", 23, 9, "谷段");
    m.onSample(makeReading(100, 60, 20, 20), 2.0, "2026-09-25", 0, 9, "谷段");
    double e = 200.0 / 3600.0;

    auto days = m.statsRows("day", 9);
    bool has24 = false, has25 = false;
    double w24 = 0, w25 = 0;
    for (auto& r : days)
    {
        if (r.key == "2026-09-24") { has24 = true; w24 = r.wh; }
        if (r.key == "2026-09-25") { has25 = true; w25 = r.wh; }
    }
    CHECK(has24 && has25, "two day buckets");
    CHECK(approx(w24, e) && approx(w25, e), "energy split across days");
}

// ---------------------------------------------------------------- 会话恢复
static void testRestore()
{
    std::printf("== session restore ==\n");
    Config cfg;
    double T0 = epochLocal(2026, 9, 24, 10);

    EnergyMeter a(cfg);
    a.restore(T0, "test", "", T0);
    std::string D = "2026-09-24";
    a.onSample(makeReading(100, 60, 20, 20), 2.0, D, 10, 9, "平段");
    a.onSample(makeReading(120, 70, 30, 20), 2.0, D, 10, 9, "平段");
    double e = 220.0 / 3600.0;
    std::string state = a.toJson(T0 + 4);

    EnergyMeter b(cfg);
    b.restore(T0, "test", state, T0 + 4);
    Snapshot sb = b.snapshot(T0 + 4, 10, 9, {}, {});
    // 持久化 round 到 4 位，容差放宽
    CHECK(approx(sb.session_wh, e, 1e-3), "same session restores session_wh");
    CHECK(sb.total_sessions == 1, "same session count stays 1");

    b.onSample(makeReading(140, 80, 40, 20), 2.0, D, 10, 9, "平段");
    double e2 = (120 + 140) / 2.0 * 2.0 / 3600.0;
    sb = b.snapshot(T0 + 6, 10, 9, {}, {});
    CHECK(approx(sb.session_wh, e + e2, 1e-3), "continues after restore");

    EnergyMeter c(cfg);
    c.restore(T0 + 50000, "test", state, T0 + 50000);
    Snapshot sc = c.snapshot(T0 + 50000, 10, 9, {}, {});
    CHECK(approx(sc.session_wh, 0.0), "new session starts empty");
    CHECK(sc.total_sessions == 2, "new session increments count");
    CHECK(approx(sc.total_wh, e, 1e-3), "new session keeps global total");
}

// ---------------------------------------------------------------- Config 往返
static void testConfigRoundtrip()
{
    std::printf("== config roundtrip ==\n");
    Config c;
    c.cpu_ppt = 105.5;
    c.price_valley_wet = 0.2;
    c.valley_wet_months = { 5,6,7 };
    c.include_monitor = true;
    std::string j = c.toJson();
    Config c2;
    CHECK(c2.fromJson(j), "parse config json");
    CHECK(approx(c2.cpu_ppt, 105.5), "cpu_ppt roundtrip");
    CHECK(approx(c2.price_valley_wet, 0.2), "valley_wet roundtrip");
    CHECK(c2.valley_wet_months.size() == 3 && c2.valley_wet_months[0] == 5, "wet months roundtrip");
    CHECK(c2.include_monitor, "include monitor roundtrip");

    const char* path = "/tmp/powermon_test_config.json";
    CHECK(c.save(path), "save config file");
    Config c3 = Config::load(path);
    CHECK(approx(c3.cpu_ppt, 105.5), "load config file");

    Config c4;
    CHECK(c4.fromJson("{\"cpu_ppt\":88}"), "partial json");
    CHECK(approx(c4.cpu_ppt, 88) && approx(c4.cpu_idle, 20.0), "missing fields default");
}

// ---------------------------------------------------------------- 字段
static void testFields()
{
    std::printf("== fields ==\n");
    CHECK(fieldDefs().size() == 15, "15 fields");

    Config cfg;
    Snapshot s;
    s.gpu_names = { "NVIDIA GeForce RTX 4090" };
    s.current_w = 132;
    s.cpu_w = 45;
    s.gpu_w = 70;
    s.base_w = 17;
    s.session_cost = 0.32;
    s.session_wh = 610;
    s.today_wh = 310;
    s.today_cost = 0.16;
    s.average_w = 110;
    s.peak_w = 168;
    s.power_on_seconds = 7 * 3600 + 21 * 60;
    s.segment = "谷段";
    s.month_wh = 12500;
    s.total_wh = 1250000;
    s.total_cost = 650.25;

    CHECK(fieldValueText("current", s, cfg) == "132 W", "current text");
    CHECK(fieldValueText("cpu", s, cfg) == "45 W", "cpu text");
    CHECK(fieldValueText("gpu", s, cfg) == "70 W", "gpu text");
    CHECK(fieldValueText("cost", s, cfg) == (std::string("\xC2\xA5") + "0.32"), "cost text");
    CHECK(fieldValueText("session", s, cfg) == "610 Wh", "session Wh");
    s.session_wh = 1600;
    CHECK(fieldValueText("session", s, cfg) == "1.60 kWh", "session kWh");
    CHECK(fieldValueText("today", s, cfg) == "0.31 kWh", "today text");
    CHECK(fieldValueText("uptime", s, cfg) == "7h21", "uptime text");
    CHECK(fieldValueText("segment", s, cfg) == "谷段", "segment text");
    CHECK(fieldValueText("month", s, cfg) == "12.5 kWh", "month text");
    CHECK(fieldValueText("total", s, cfg) == "1250.00 kWh", "total text");
    CHECK(fieldValueText("total_cost", s, cfg) == (std::string("\xC2\xA5") + "650.25"), "total cost text");

    Snapshot s2;
    CHECK(fieldValueText("gpu", s2, cfg) == "N/A", "gpu N/A when no names");
}

int main()
{
    testParseHours();
    testRegions();
    testTariff();
    testMeter();
    testCrossDay();
    testRestore();
    testConfigRoundtrip();
    testFields();

    std::printf("\n=====================================\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    std::printf("=====================================\n");
    return g_fail == 0 ? 0 : 1;
}
