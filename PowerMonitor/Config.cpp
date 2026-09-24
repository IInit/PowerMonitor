// Config.cpp : 配置 JSON 序列化实现（基于 yyjson）
#include "Config.h"
#include "FileUtil.h"
#include "yyjson/yyjson.h"
#include <cstdlib>

// 数字字段读取：兼容 int / real
static double GetNum(yyjson_val* v)
{
    if (v == nullptr)
        return 0.0;
    if (yyjson_is_real(v))
        return yyjson_get_real(v);
    if (yyjson_is_int(v) || yyjson_is_uint(v))
        return (double)yyjson_get_int(v);
    return 0.0;
}

static bool GetBool(yyjson_val* v) { return v && yyjson_is_bool(v) ? yyjson_get_bool(v) : false; }
static std::string GetStr(yyjson_val* v) { return (v && yyjson_is_str(v)) ? std::string(yyjson_get_str(v)) : std::string(); }

std::string Config::toJson() const
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_real(doc, root, "cpu_ppt", cpu_ppt);
    yyjson_mut_obj_add_real(doc, root, "cpu_idle", cpu_idle);
    yyjson_mut_obj_add_real(doc, root, "cpu_load_exponent", cpu_load_exponent);
    yyjson_mut_obj_add_real(doc, root, "baseline_watts", baseline_watts);

    yyjson_mut_obj_add_real(doc, root, "price_peak", price_peak);
    yyjson_mut_obj_add_real(doc, root, "price_flat", price_flat);
    yyjson_mut_obj_add_real(doc, root, "price_valley_dry", price_valley_dry);
    yyjson_mut_obj_add_real(doc, root, "price_valley_wet", price_valley_wet);

    yyjson_mut_val* wm = yyjson_mut_arr(doc);
    for (int m : valley_wet_months)
        yyjson_mut_arr_add_int(doc, wm, m);
    yyjson_mut_obj_add_val(doc, root, "valley_wet_months", wm);

    yyjson_mut_obj_add_str(doc, root, "peak_hours", peak_hours.c_str());
    yyjson_mut_obj_add_str(doc, root, "valley_hours", valley_hours.c_str());

    yyjson_mut_obj_add_str(doc, root, "tariff_region", tariff_region.c_str());
    yyjson_mut_obj_add_str(doc, root, "tariff_plan", tariff_plan.c_str());
    yyjson_mut_obj_add_str(doc, root, "tariff_source", tariff_source.c_str());
    yyjson_mut_obj_add_str(doc, root, "tariff_effective", tariff_effective.c_str());
    yyjson_mut_obj_add_bool(doc, root, "tariff_verify", tariff_verify);
    yyjson_mut_obj_add_str(doc, root, "tariff_note", tariff_note.c_str());
    yyjson_mut_obj_add_str(doc, root, "currency", currency.c_str());

    yyjson_mut_obj_add_real(doc, root, "sample_interval", sample_interval);
    yyjson_mut_obj_add_bool(doc, root, "include_monitor", include_monitor);
    yyjson_mut_obj_add_real(doc, root, "monitor_watts", monitor_watts);
    yyjson_mut_obj_add_real(doc, root, "calibration", calibration);

    size_t out_len = 0;
    char* str = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &out_len);
    std::string result;
    if (str != nullptr)
    {
        result = str;
        std::free(str);
    }
    yyjson_mut_doc_free(doc);
    return result;
}

bool Config::fromJson(const std::string& json_text)
{
    if (json_text.empty())
        return false;
    std::string buf = json_text;   // yyjson 0.4 需要可写缓冲区
    yyjson_doc* doc = yyjson_read_opts(&buf[0], buf.size(), 0, nullptr, nullptr);
    if (doc == nullptr)
        return false;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root))
    {
        yyjson_doc_free(doc);
        return false;
    }

    if (auto v = yyjson_obj_get(root, "cpu_ppt")) cpu_ppt = GetNum(v);
    if (auto v = yyjson_obj_get(root, "cpu_idle")) cpu_idle = GetNum(v);
    if (auto v = yyjson_obj_get(root, "cpu_load_exponent")) cpu_load_exponent = GetNum(v);
    if (auto v = yyjson_obj_get(root, "baseline_watts")) baseline_watts = GetNum(v);

    if (auto v = yyjson_obj_get(root, "price_peak")) price_peak = GetNum(v);
    if (auto v = yyjson_obj_get(root, "price_flat")) price_flat = GetNum(v);
    if (auto v = yyjson_obj_get(root, "price_valley_dry")) price_valley_dry = GetNum(v);
    if (auto v = yyjson_obj_get(root, "price_valley_wet")) price_valley_wet = GetNum(v);

    if (auto v = yyjson_obj_get(root, "valley_wet_months"); v && yyjson_is_arr(v))
    {
        valley_wet_months.clear();
        size_t n = yyjson_arr_size(v);
        for (size_t i = 0; i < n; ++i)
        {
            yyjson_val* e = yyjson_arr_get(v, i);
            int m = (int)GetNum(e);
            if (m >= 1 && m <= 12)
                valley_wet_months.push_back(m);
        }
    }

    if (auto v = yyjson_obj_get(root, "peak_hours")) peak_hours = GetStr(v);
    if (auto v = yyjson_obj_get(root, "valley_hours")) valley_hours = GetStr(v);

    if (auto v = yyjson_obj_get(root, "tariff_region")) tariff_region = GetStr(v);
    if (auto v = yyjson_obj_get(root, "tariff_plan")) tariff_plan = GetStr(v);
    if (auto v = yyjson_obj_get(root, "tariff_source")) tariff_source = GetStr(v);
    if (auto v = yyjson_obj_get(root, "tariff_effective")) tariff_effective = GetStr(v);
    if (auto v = yyjson_obj_get(root, "tariff_verify")) tariff_verify = GetBool(v);
    if (auto v = yyjson_obj_get(root, "tariff_note")) tariff_note = GetStr(v);
    if (auto v = yyjson_obj_get(root, "currency")) { std::string c = GetStr(v); if (!c.empty()) currency = c; }

    if (auto v = yyjson_obj_get(root, "sample_interval")) sample_interval = GetNum(v);
    if (auto v = yyjson_obj_get(root, "include_monitor")) include_monitor = GetBool(v);
    if (auto v = yyjson_obj_get(root, "monitor_watts")) monitor_watts = GetNum(v);
    if (auto v = yyjson_obj_get(root, "calibration")) calibration = GetNum(v);

    yyjson_doc_free(doc);
    return true;
}

bool Config::save(const std::string& utf8_path) const
{
    return FileUtil::writeFileAtomic(utf8_path, toJson());
}

Config Config::load(const std::string& utf8_path)
{
    Config cfg;
    std::string text;
    if (FileUtil::readFile(utf8_path, text))
        cfg.fromJson(text);
    return cfg;
}
