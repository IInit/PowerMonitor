// Config.h : 插件配置（硬件功耗模型、采样、校准系数与电价）
// 本文件为跨平台纯数据层，不依赖 Windows / MFC，可在 Linux g++ 下直接编译测试。
// 文本一律使用 UTF-8 std::string；在插件接口（wchar_t*）边界再做编码转换。
#pragma once

#include <string>
#include <vector>

struct Config
{
    // ---- 硬件功耗模型 ----
    double cpu_ppt = 142.0;             // CPU 封装功耗上限 W（5800X PPT）
    double cpu_idle = 20.0;             // CPU 空载封装功耗 W
    double cpu_load_exponent = 0.85;    // 负载 -> 功耗曲线指数
    double baseline_watts = 35.0;       // 主板+内存+存储+风扇等固定开销 W

    // ---- 计价：默认四川「一户一表」第一档 ----
    double price_peak = 0.5224;         // 峰段电价（元/度）
    double price_flat = 0.5224;         // 平段电价
    double price_valley_dry = 0.2535;   // 谷段电价（枯/平水期）
    double price_valley_wet = 0.175;    // 谷段电价（丰水期）
    std::vector<int> valley_wet_months = { 6, 7, 8, 9, 10 };

    std::string peak_hours;             // 峰段时段，空=无独立峰段
    std::string valley_hours = "23-7";  // 谷段时段，空=无低谷优惠

    // ---- 预设来源（展示用） ----
    std::string tariff_region = "四川";
    std::string tariff_plan = "一户一表 第一档";
    std::string tariff_source = "四川省电网居民生活电价表（川发改价格〔2012〕560号、〔2026〕255号）";
    std::string tariff_effective = "2026-07-01";
    bool tariff_verify = false;         // true=网络汇总口径，建议核对
    std::string tariff_note = "7-9月第一档电量为260度及以下，其他月份180度及以下";

    std::string currency = "\xC2\xA5";  // ¥ 的 UTF-8 编码

    // ---- 采样 ----
    double sample_interval = 2.0;       // 期望采样间隔（秒），实际以主程序 DataRequired 节奏为准

    // ---- 整机口径 ----
    bool include_monitor = false;       // 是否把显示器功耗计入
    double monitor_watts = 30.0;        // 显示器功耗 W
    double calibration = 1.0;           // 整机校准系数（直流口径 -> 插座口径）

    // ---- 序列化（JSON，UTF-8） ----
    // 序列化为 JSON 文本；fromJson 从 JSON 文本加载，未知字段忽略、缺失字段用默认值。
    std::string toJson() const;
    bool fromJson(const std::string& json_text);

    // 文件读写便捷封装（内部走 FileUtil，Windows 使用宽字符路径）
    bool save(const std::string& utf8_path) const;
    static Config load(const std::string& utf8_path);
};
