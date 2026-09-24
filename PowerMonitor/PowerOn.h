// PowerOn.h : 本次开机时刻判定。仅 Windows。
#pragma once
#include <string>

struct PowerOnInfo
{
    double epoch = 0.0;       // 开机时刻 epoch 秒
    std::string source;       // 来源描述
};

PowerOnInfo detectPowerOn();
