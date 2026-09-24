// Tariffs.cpp : 居民电价预设与分时计价实现
#include "Tariffs.h"
#include <algorithm>

// 把 "8-22" / "11-17,20-22" / "23-7" 解析为小时集合（左闭右开，支持跨零点）
std::set<int> parseHours(const std::string& spec)
{
    std::set<int> hours;
    // 统一把全角逗号“，”(U+FF0C, UTF-8 EF BC 8C) 替换为半角逗号
    std::string normalized;
    {
        const std::string full = "\xEF\xBC\x8C"; // ，
        size_t pos = 0, found = 0;
        std::string tmp = spec;
        while ((found = tmp.find(full, pos)) != std::string::npos)
        {
            tmp.replace(found, full.size(), ",");
            pos = found + 1;
        }
        normalized = tmp;
    }

    size_t start = 0;
    while (start <= normalized.size())
    {
        size_t comma = normalized.find(',', start);
        std::string chunk = normalized.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        // trim
        size_t b = chunk.find_first_not_of(" \t");
        size_t e = chunk.find_last_not_of(" \t");
        if (b != std::string::npos)
            chunk = chunk.substr(b, e - b + 1);
        else
            chunk.clear();

        if (!chunk.empty())
        {
            size_t dash = chunk.find('-');
            if (dash != std::string::npos)
            {
                int st = 0, en = 0;
                bool ok1 = false, ok2 = false;
                try { st = std::stoi(chunk.substr(0, dash)); ok1 = true; } catch (...) {}
                try { en = std::stoi(chunk.substr(dash + 1)); ok2 = true; } catch (...) {}
                if (ok1 && ok2)
                {
                    if (st == en)
                    {
                        hours.insert(((st % 24) + 24) % 24);
                    }
                    else
                    {
                        int cur = ((st % 24) + 24) % 24;
                        int end = ((en % 24) + 24) % 24;
                        for (int i = 0; i < 24; ++i)
                        {
                            hours.insert(cur);
                            cur = (cur + 1) % 24;
                            if (cur == end)
                                break;
                        }
                    }
                }
            }
            else
            {
                try
                {
                    int v = std::stoi(chunk);
                    hours.insert(((v % 24) + 24) % 24);
                }
                catch (...) {}
            }
        }

        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return hours;
}

// 生成不分时三档：第一档/第二档/第三档（不分时）
static std::vector<Plan> flatTiers(double t1, double t2, double t3, const std::string& note)
{
    std::vector<Plan> v;
    double ps[3] = { t1, t2, t3 };
    const char* names[3] = { "第一档（不分时）", "第二档（不分时）", "第三档（不分时）" };
    for (int i = 0; i < 3; ++i)
    {
        Plan p;
        p.label = names[i];
        p.peak = p.flat = p.valley = ps[i];
        p.note = note;
        v.push_back(p);
    }
    return v;
}

const std::vector<Region>& allRegions()
{
    static const std::vector<Region> regions = []() {
        std::vector<Region> r;

        {
            Region x;
            x.name = "四川";
            x.plans = {
                Plan{ "一户一表 第一档", 0.5224, 0.5224, 0.2535, 0.175, "", "23-7", {6,7,8,9,10}, "7-9月第一档电量为260度及以下，其他月份180度及以下" },
                Plan{ "一户一表 第二档", 0.6224, 0.6224, 0.3535, 0.275, "", "23-7", {6,7,8,9,10}, "" },
                Plan{ "一户一表 第三档", 0.8224, 0.8224, 0.5535, 0.475, "", "23-7", {6,7,8,9,10}, "" },
                Plan{ "合表用户", 0.5464, 0.5464, 0.5464, 0, "", "", {}, "合表用户不执行低谷优惠" },
            };
            x.source = "四川省电网居民生活电价表（川发改价格〔2012〕560号、〔2026〕255号）";
            x.effective = "2026-07-01";
            x.keywords = { "成都","绵阳","德阳","自贡","泸州","南充","宜宾" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "浙江";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 0.5680, 0.5380, 0.2880, 0, "8-22", "22-8", {}, "年用电2760度及以下" },
                Plan{ "一户一表 第二档 · 峰谷", 0.6180, 0.5880, 0.3380, 0, "8-22", "22-8", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 0.8680, 0.8380, 0.5880, 0, "8-22", "22-8", {}, "" },
                Plan{ "一户一表 不分时", 0.5380, 0.5380, 0.5380, 0, "", "", {}, "" },
            };
            x.source = "国网浙江电力 居民生活用电分时电价表";
            x.keywords = { "杭州","宁波","温州","金华","绍兴" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "安徽";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 0.5953, 0.5953, 0.3153, 0, "", "22-8", {}, "平段8:00-22:00在分档价基础上加0.03元，谷段减0.25元" },
                Plan{ "一户一表 第二档 · 峰谷", 0.6453, 0.6453, 0.3653, 0, "", "22-8", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 0.8953, 0.8953, 0.6153, 0, "", "22-8", {}, "" },
                Plan{ "一户一表 不分时", 0.5653, 0.5653, 0.5653, 0, "", "", {}, "" },
            };
            x.source = "阜阳市人民政府 电价知识库（安徽一户一表居民电价标准）";
            x.keywords = { "合肥","芜湖","蚌埠","安庆" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "山东";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 0.5769, 0.5469, 0.3769, 0, "8-22", "22-8", {}, "采暖季（11月-次年3月）谷价再降至0.3469元，且峰段改为8:00-20:00" },
                Plan{ "一户一表 第二档 · 峰谷", 0.6269, 0.5969, 0.4269, 0, "8-22", "22-8", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 0.8769, 0.8469, 0.6769, 0, "8-22", "22-8", {}, "" },
                Plan{ "一户一表 不分时", 0.5469, 0.5469, 0.5469, 0, "", "", {}, "" },
            };
            x.source = "山东电网销售电价表（鲁发改价格〔2026〕556号）、居民分时电价（鲁发改价格〔2022〕158号）";
            x.effective = "2026-07-30";
            x.keywords = { "济南","青岛","烟台","潍坊","临沂" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "河南";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 0.59, 0.56, 0.44, 0, "8-22", "22-8", {}, "峰段加0.03元、谷段减0.12元；“煤改电”用户采暖季谷段延长为20:00-次日8:00" },
                Plan{ "一户一表 第二档 · 峰谷", 0.64, 0.61, 0.49, 0, "8-22", "22-8", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 0.89, 0.86, 0.74, 0, "8-22", "22-8", {}, "" },
                Plan{ "一户一表 不分时", 0.56, 0.56, 0.56, 0, "", "", {}, "" },
            };
            x.source = "河南省发展和改革委员会《关于进一步完善分时电价机制有关事项的通知》";
            x.keywords = { "郑州","洛阳","南阳","新乡" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "重庆";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 0.62, 0.52, 0.34, 0, "11-17,20-22", "0-8", {}, "峰段加0.10元、谷段减0.18元；执行满一年后可申请退出" },
                Plan{ "一户一表 第二档 · 峰谷", 0.67, 0.57, 0.39, 0, "11-17,20-22", "0-8", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 0.92, 0.82, 0.64, 0, "11-17,20-22", "0-8", {}, "" },
                Plan{ "一户一表 不分时", 0.52, 0.52, 0.52, 0, "", "", {}, "" },
            };
            x.source = "重庆市人民政府《建立重庆市居民分时电价机制》（2023-06-01 执行）";
            x.keywords = { "渝中","万州","涪陵","潼南" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "广东（深圳）";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 1.1121, 0.6542, 0.2486, 0, "10-12,14-19", "0-8", {}, "深圳供电局口径，不含政府性基金及附加；夏季（5-10月）第一档0-260度，非夏季0-200度" },
                Plan{ "一户一表 第二档 · 峰谷", 1.1621, 0.7042, 0.2986, 0, "10-12,14-19", "0-8", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 1.4121, 0.9542, 0.5486, 0, "10-12,14-19", "0-8", {}, "" },
                Plan{ "一户一表 不分时", 0.6542, 0.6542, 0.6542, 0, "", "", {}, "" },
                Plan{ "合表用户", 0.6912, 0.6912, 0.6912, 0, "", "", {}, "" },
            };
            x.source = "深圳市发展和改革委员会 深圳市居民生活电价价目表";
            x.keywords = { "深圳","广州","东莞","佛山","珠海" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "贵州";
            x.plans = flatTiers(0.4556, 0.5056, 0.7556, "按自然年度累计：0-3000度、3000-4700度、4700度以上；12月1日起为新周期");
            Plan he{ "合表用户", 0.4820, 0.4820, 0.4820, 0, "", "", {}, "" };
            x.plans.push_back(he);
            x.source = "贵州电网销售电价表（黔发改价格〔2016〕1299号）、贵州省发改委 2026 年提案答复";
            x.effective = "2016-08-25";
            x.keywords = { "贵阳","遵义","六盘水","都匀","黔南" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "云南";
            x.plans = {
                Plan{ "一户一表 第一档", 0.423625, 0.423625, 0.423625, 0, "", "", {}, "每年5-11月执行统一电价0.423625元" },
                Plan{ "一户一表 第二档（12月-4月）", 0.473625, 0.473625, 0.473625, 0, "", "", {}, "仅每年12月至次年4月执行阶梯" },
                Plan{ "一户一表 第三档（12月-4月）", 0.773625, 0.773625, 0.773625, 0, "", "", {}, "仅每年12月至次年4月执行阶梯" },
                Plan{ "合表用户", 0.483625, 0.483625, 0.483625, 0, "", "", {}, "" },
            };
            x.source = "云南电网销售电价表";
            x.keywords = { "昆明","大理","曲靖","红河" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "广西";
            x.plans = flatTiers(0.5283, 0.5783, 0.8283, "按自然年度累计：0-3120度、3120-4440度、4440度以上");
            Plan he{ "合表用户", 0.5491, 0.5491, 0.5491, 0, "", "", {}, "" };
            x.plans.push_back(he);
            x.source = "柳州市政府定价商品价格目录清单（桂发改价格〔2021〕16号、桂价格函〔2018〕172号）";
            x.effective = "2021-10-24";
            x.keywords = { "南宁","柳州","桂林","北海" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "海南";
            x.plans = flatTiers(0.6083, 0.6583, 0.9083, "夏季（4-10月）第一档0-220度、第二档221-360度；冬季档位更少");
            Plan he{ "合表用户", 0.6295, 0.6295, 0.6295, 0, "", "", {}, "" };
            x.plans.push_back(he);
            x.source = "海南省 12345 政务服务热线 电价答复口径";
            x.keywords = { "海口","三亚","文昌","昌江" };
            r.push_back(x);
        }

        // ---- 以下为阶梯 + 可选峰谷地区 ----
        {
            Region x;
            x.name = "北京";
            x.plans = flatTiers(0.4883, 0.5383, 0.7883, "年累计：0-2880度、2880-4800度、4800度以上");
            x.plans.push_back(Plan{ "一户一表 · 峰谷", 0.5603, 0.5103, 0.3103, 0, "8-22", "22-8", {}, "网络汇总口径，峰谷时段与价格建议向国网北京核实" });
            x.source = "国网各省现行居民阶梯电价汇总（2026），建议以国网北京官网为准";
            x.verify = true;
            r.push_back(x);
        }
        {
            Region x;
            x.name = "上海";
            x.plans = {
                Plan{ "一户一表 第一档 · 峰谷", 0.617, 0.617, 0.307, 0, "6-22", "22-6", {}, "年累计0-3120度为第一档；峰谷时段6:00-22:00 / 22:00-次日6:00" },
                Plan{ "一户一表 第二档 · 峰谷", 0.667, 0.667, 0.357, 0, "6-22", "22-6", {}, "" },
                Plan{ "一户一表 第三档 · 峰谷", 0.917, 0.917, 0.607, 0, "6-22", "22-6", {}, "" },
                Plan{ "一户一表 不分时", 0.617, 0.617, 0.617, 0, "", "", {}, "" },
            };
            x.source = "网络公开汇总口径，建议以国网上海官网 / 电费账单为准";
            x.verify = true;
            x.keywords = { "上海" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "江苏";
            x.plans = flatTiers(0.5283, 0.5783, 0.8283, "年累计：0-2760度、2760-4800度、4800度以上；江苏可申请居民峰谷，具体峰谷价请按电费账单填写");
            x.plans.push_back(Plan{ "一户一表 · 峰谷（按账单填写）", 0.5583, 0.5283, 0.3583, 0, "8-21", "21-8", {}, "峰谷价差为常见口径，务必按自己电费账单核对" });
            x.source = "国网各省现行居民阶梯电价汇总（2026）；峰谷值需用户按账单核对";
            x.verify = true;
            x.keywords = { "南京","苏州","无锡","常州","徐州" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "福建";
            x.plans = flatTiers(0.598, 0.648, 0.898, "年累计：0-2760度、2760-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "福州","厦门","泉州" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "湖北";
            x.plans = flatTiers(0.558, 0.608, 0.858, "年累计：0-2160度、2160-4800度、4800度以上");
            x.plans.push_back(Plan{ "一户一表 · 峰谷（按账单填写）", 0.62, 0.558, 0.36, 0, "9-15,20-22", "23-7", {}, "湖北峰谷时段：尖峰20-22、高峰9-15、低谷23-次日7；峰谷浮动系数为工商业口径，居民请按账单核对" });
            x.source = "国网各省现行居民阶梯电价汇总（2026）、湖北省发改委峰谷分时电价说明";
            x.verify = true;
            x.keywords = { "武汉","宜昌","襄阳" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "湖南";
            x.plans = flatTiers(0.588, 0.638, 0.888, "按月阶梯，冬夏季电量上限动态调整");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "长沙","株洲","湘潭" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "江西";
            x.plans = flatTiers(0.60, 0.65, 0.90, "年累计：0-2160度、2160-4800度");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "南昌","赣州","九江" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "河北";
            x.plans = flatTiers(0.52, 0.57, 0.82, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "石家庄","唐山","保定" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "山西";
            x.plans = flatTiers(0.477, 0.527, 0.777, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "太原","大同","临汾" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "陕西";
            x.plans = flatTiers(0.4983, 0.5483, 0.7983, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "西安","宝鸡","咸阳" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "天津";
            x.plans = flatTiers(0.51, 0.56, 0.81, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            r.push_back(x);
        }
        {
            Region x;
            x.name = "辽宁";
            x.plans = flatTiers(0.50, 0.55, 0.80, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "沈阳","大连","鞍山" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "吉林";
            x.plans = flatTiers(0.525, 0.575, 0.825, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "长春","吉林","延边" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "黑龙江";
            x.plans = flatTiers(0.51, 0.56, 0.81, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "哈尔滨","大庆","齐齐哈尔" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "甘肃";
            x.plans = flatTiers(0.51, 0.56, 0.81, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "兰州","天水" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "青海";
            x.plans = flatTiers(0.5142, 0.5642, 0.8142, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "西宁" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "宁夏";
            x.plans = flatTiers(0.49, 0.54, 0.79, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "银川" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "新疆";
            x.plans = flatTiers(0.52, 0.57, 0.82, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "乌鲁木齐","喀什" };
            r.push_back(x);
        }
        {
            Region x;
            x.name = "内蒙古";
            x.plans = flatTiers(0.50, 0.55, 0.80, "年累计：0-2160度、2160-4800度、4800度以上");
            x.source = "国网各省现行居民阶梯电价汇总（2026）";
            x.verify = true;
            x.keywords = { "呼和浩特","包头" };
            r.push_back(x);
        }

        return r;
    }();
    return regions;
}

std::vector<std::string> regionNames()
{
    std::vector<std::string> names;
    for (const Region& r : allRegions())
        names.push_back(r.name);
    return names;
}

const Region* findRegion(const std::string& name)
{
    std::string target = name;
    // trim
    size_t b = target.find_first_not_of(" \t");
    size_t e = target.find_last_not_of(" \t");
    if (b != std::string::npos) target = target.substr(b, e - b + 1); else target.clear();
    if (target.empty())
        return nullptr;

    for (const Region& r : allRegions())
        if (r.name == target)
            return &r;
    for (const Region& r : allRegions())
        if (target.find(r.name) != std::string::npos || r.name.find(target) != std::string::npos)
            return &r;
    for (const Region& r : allRegions())
        for (const std::string& k : r.keywords)
            if (target.find(k) != std::string::npos)
                return &r;
    return nullptr;
}

std::vector<std::string> planLabels(const Region& region)
{
    std::vector<std::string> labels;
    for (const Plan& p : region.plans)
        labels.push_back(p.label);
    return labels;
}

void applyPlan(Config& cfg, const Region& region, const Plan& plan)
{
    cfg.tariff_region = region.name;
    cfg.tariff_plan = plan.label;
    cfg.tariff_source = region.source;
    cfg.tariff_effective = region.effective;
    cfg.tariff_verify = region.verify;
    cfg.price_peak = plan.peak;
    cfg.price_flat = plan.flat;
    cfg.price_valley_dry = plan.valley;
    cfg.price_valley_wet = plan.resolvedValleyWet();
    cfg.peak_hours = plan.peak_hours;
    cfg.valley_hours = plan.valley_hours;
    cfg.valley_wet_months = plan.wet_months;
    cfg.tariff_note = plan.note;
}

// ---- 分时计价 ----
std::string segmentOf(const Config& cfg, int hour)
{
    std::set<int> peak = parseHours(cfg.peak_hours);
    if (!peak.empty() && peak.count(hour))
        return "\xE5\xB3\xB0\xE6\xAE\xB5"; // 峰段
    std::set<int> valley = parseHours(cfg.valley_hours);
    if (!valley.empty() && valley.count(hour))
        return "\xE8\xB0\xB7\xE6\xAE\xB5"; // 谷段
    return "\xE5\xB9\xB3\xE6\xAE\xB5";       // 平段
}

double valleyRate(const Config& cfg, int month)
{
    bool wet = false;
    for (int m : cfg.valley_wet_months)
        if (m == month) { wet = true; break; }
    return wet ? cfg.price_valley_wet : cfg.price_valley_dry;
}

double blendCost(const Config& cfg, double wh, double peak_wh, double valley_wh, int month)
{
    if (wh <= 0.0)
        return 0.0;
    double pk = (std::min)((std::max)(0.0, peak_wh), wh);
    double vy = (std::min)((std::max)(0.0, valley_wh), wh - pk);
    double flat_wh = wh - pk - vy;
    return (pk * cfg.price_peak + flat_wh * cfg.price_flat + vy * valleyRate(cfg, month)) / 1000.0;
}

void rateAt(const Config& cfg, int hour, int month, std::string& segment, double& rate)
{
    segment = segmentOf(cfg, hour);
    if (segment == "\xE5\xB3\xB0\xE6\xAE\xB5")
        rate = cfg.price_peak;
    else if (segment == "\xE8\xB0\xB7\xE6\xAE\xB5")
        rate = valleyRate(cfg, month);
    else
        rate = cfg.price_flat;
}
