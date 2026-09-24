// Tariffs.h : 居民电价预设与分时计价（计价为纯函数）
// 跨平台纯算法层，不依赖 Windows / MFC。
#pragma once

#include "Config.h"
#include <set>
#include <string>
#include <vector>

// 一个用电方案（同一地区可有多个档位 / 计费制式）
struct Plan
{
    std::string label;
    double peak = 0.0;        // 峰段电价
    double flat = 0.0;        // 平段电价
    double valley = 0.0;      // 谷段电价（枯/平水期）
    double valley_wet = 0.0;  // 谷段电价（丰水期）；0 表示无丰枯差
    std::string peak_hours;   // 峰段时段，空=无独立峰段
    std::string valley_hours; // 谷段时段，空=无低谷优惠
    std::vector<int> wet_months;
    std::string note;

    double resolvedValleyWet() const { return valley_wet > 0.0 ? valley_wet : valley; }
};

// 一个省级行政区
struct Region
{
    std::string name;
    std::vector<Plan> plans;
    std::string source;
    std::string effective;
    bool verify = false;      // true=仅网络汇总口径，建议核对
    std::vector<std::string> keywords;

    const Plan& defaultPlan() const { return plans[0]; }
};

// 把 "8-22" / "11-17,20-22" / "23-7" 解析为小时集合（左闭右开，支持跨零点）
std::set<int> parseHours(const std::string& spec);

// 全部地区预设（30 个省级行政区）
const std::vector<Region>& allRegions();

// 地区 / 方案查询
std::vector<std::string> regionNames();
const Region* findRegion(const std::string& name);   // 支持名称、包含、城市关键词反查
std::vector<std::string> planLabels(const Region& region);

// 把预设写入配置对象（不落盘）
void applyPlan(Config& cfg, const Region& region, const Plan& plan);

// ---- 分时计价纯函数 ----
// 当前时段名称：峰段 / 平段 / 谷段（峰优先于谷，都不匹配为平段）
std::string segmentOf(const Config& cfg, int hour);

// 谷段单价：按 month 是否处于丰水期决定
double valleyRate(const Config& cfg, int month);

// 由电量按当前电价算电费（元）。wh 为总电量 Wh，peak_wh/valley_wh 为峰谷拆分。
// 峰谷超出总量时夹回，保证平段非负。
double blendCost(const Config& cfg, double wh, double peak_wh, double valley_wh, int month);

// 当前时段及其电价
void rateAt(const Config& cfg, int hour, int month, std::string& segment, double& rate);
