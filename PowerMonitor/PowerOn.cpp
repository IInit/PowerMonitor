// PowerOn.cpp : 本次开机时刻判定实现
#include "pch.h"
#include "PowerOn.h"
#include <winevt.h>
#include <evntcons.h>
#include <ctime>

#pragma comment(lib, "wevtapi.lib")

namespace
{
    // FILETIME(100ns since 1601) -> epoch 秒
    double fileTimeToEpoch(ULONGLONG ft100)
    {
        return double(ft100 - 116444736000000000ULL) / 10000000.0;
    }

    // 单调不偏时钟（剔除睡眠），失败返回 false
    bool queryUnbiased(ULONGLONG& hundred_ns)
    {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (!k32)
            return false;
        typedef BOOL(WINAPI *PFN_QUBI)(PULONGLONG);
        auto fn = (PFN_QUBI)GetProcAddress(k32, "QueryUnbiasedInterruptTime");
        if (!fn)
            return false;
        return fn(&hundred_ns) != FALSE;
    }

    // 从 System 日志反向找最新一条 Kernel-Boot(30) 事件，返回其 UTC epoch
    bool kernelBootFromEventLog(double& out_epoch)
    {
        EVT_HANDLE hQuery = EvtQuery(
            nullptr,
            L"System",
            L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Boot'] and EventID=30]]",
            EvtQueryChannelPath | EvtQueryReverseDirection);
        if (!hQuery)
            return false;

        bool found = false;
        EVT_HANDLE hEvent = nullptr;
        DWORD returned = 0;
        if (EvtNext(hQuery, 1, &hEvent, INFINITE, 0, &returned) && returned == 1)
        {
            DWORD needed = 0, property_count = 0;
            // 第一次取所需缓冲大小
            EvtRender(nullptr, hEvent, EvtRenderEventValues, 0, nullptr, &needed, &property_count);
            if (needed > 0)
            {
                std::vector<unsigned char> buf(needed);
                if (EvtRender(nullptr, hEvent, EvtRenderEventValues, needed,
                              buf.data(), &needed, &property_count))
                {
                    const EVT_VARIANT* values = reinterpret_cast<const EVT_VARIANT*>(buf.data());
                    // 缓冲区首元素为属性个数，其后为各属性值
                    DWORD max_elems = (DWORD)(buf.size() / sizeof(EVT_VARIANT));
                    DWORD count = property_count + 1;
                    if (count > max_elems)
                        count = max_elems;
                    for (DWORD i = 0; i < count; ++i)
                    {
                        if (values[i].Type == EvtVarTypeFileTime)
                        {
                            // EVT_VARIANT::FileTimeVal 为 100ns 计数（ULONGLONG）
                            out_epoch = fileTimeToEpoch(values[i].FileTimeVal);
                            found = true;
                            break;
                        }
                    }
                }
            }
            EvtClose(hEvent);
        }
        EvtClose(hQuery);
        return found;
    }
}

PowerOnInfo detectPowerOn()
{
    PowerOnInfo info;

    // 当前时刻与内核运行时长（100ns）
    ULONGLONG now100 = 0;
    GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&now100));

    ULONGLONG unbiased100 = 0;
    bool have_unbiased = queryUnbiased(unbiased100);
    ULONGLONG boot_kernel100 = have_unbiased ? (now100 - unbiased100) : 0;

    double boot_epoch = 0.0;
    if (kernelBootFromEventLog(boot_epoch))
    {
        ULONGLONG boot100 = (ULONGLONG)((boot_epoch * 10000000.0) + 116444736000000000.0);
        // 事件时间必须在本次内核会话内（防止取到上一次开机的残留日志）
        if (!have_unbiased || boot100 >= boot_kernel100)
        {
            info.epoch = boot_epoch;
            info.source = "System \xE6\x97\xA5\xE5\xBF\x97 Kernel-Boot"; // System 日志 Kernel-Boot
            return info;
        }
    }

    // 回退 1：不偏中断时钟
    if (have_unbiased)
    {
        info.epoch = fileTimeToEpoch(boot_kernel100);
        info.source = "QueryUnbiasedInterruptTime";
        return info;
    }

    // 回退 2：GetTickCount64（含睡眠计数，精度差）
    ULONGLONG tick_ms = GetTickCount64();
    if (tick_ms > 0)
    {
        info.epoch = double(now100 - 116444736000000000ULL) / 10000000.0 - double(tick_ms) / 1000.0;
        info.source = "GetTickCount64";
        return info;
    }

    // 回退 3：当前时刻
    info.epoch = (double)std::time(nullptr);
    info.source = "-";
    return info;
}
