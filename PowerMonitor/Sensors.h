// Sensors.h : Windows 功耗数据采集（PDH CPU 负载模型 + NVML GPU 实测 + 固定开销）
// 仅 Windows 实现；细节以 pimpl 藏于 .cpp。
#pragma once

#include "Meter.h"
#include <string>
#include <vector>

class SensorHub
{
public:
    explicit SensorHub(const Config& cfg);
    ~SensorHub();

    // 采集一次，返回 Reading（wall_ts 已填）
    Reading read();

    // 配置变更后重载 CPU 模型参数
    void reload(const Config& cfg);

    const std::vector<std::string>& gpuNames() const;
    const std::vector<double>& gpuLimits() const;

private:
    struct Impl;
    Impl* m_impl;
};
