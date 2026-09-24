// PowerMonitor.h : 插件主类（ITMPlugin），把采集 / 积分 / 账本 / 界面串起来
#pragma once

#include "include/PluginInterface.h"
#include "Config.h"
#include "Meter.h"
#include "Sensors.h"
#include "PowerOn.h"
#include <memory>
#include <string>
#include <vector>

class CPowerMonitorItem;

class CPowerMonitor : public ITMPlugin
{
public:
    CPowerMonitor();
    virtual ~CPowerMonitor();

    // ---------------- ITMPlugin ----------------
    // 注意：ITMPlugin 没有 GetItemCount，主程序会从 index=0 开始调用
    // GetItem()，直到返回 nullptr 为止。
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void* GetPluginIcon() override;

    virtual int GetCommandCount() override;
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual void* GetCommandIcon(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    virtual int IsCommandChecked(int command_index) override;

    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;

    // ---------------- 供显示项查询（UTF-8） ----------------
    std::string itemFullLabelUtf8(const std::string& key) const;
    std::string itemShortLabelUtf8(const std::string& key) const;
    std::string itemValueUtf8(const std::string& key) const;
    std::string itemSampleUtf8(const std::string& key) const;

    // ---------------- 供对话框使用 ----------------
    Config& editableConfig() { return m_cfg; }
    EnergyMeter& meter() { return m_meter; }
    // 应用配置：更新 meter / sensors 并落盘。
    // 返回 false 表示配置未能写入磁盘，失败原因写入 err（可直接用于 UI 提示）。
    bool applyConfigChanged(std::wstring* err = nullptr);
    void persistState();
    void resetCurrentSession();

private:
    void initialize();
    static double nowEpoch();
    double localTimeContext(int& hour, int& month, std::string& today) const;

    Config m_cfg;
    EnergyMeter m_meter;
    std::unique_ptr<SensorHub> m_sensors;
    std::vector<std::unique_ptr<CPowerMonitorItem>> m_items;

    ITrafficMonitor* m_app = nullptr;
    std::wstring m_config_dir;
    std::string m_config_path;   // UTF-8
    std::string m_state_path;    // UTF-8

    Snapshot m_snapshot;
    bool m_inited = false;

    LARGE_INTEGER m_qpc_freq{};
    LARGE_INTEGER m_last_qpc{};
    bool m_have_last_qpc = false;

    double m_last_persist = 0.0;

    mutable std::wstring m_info_cache;
    mutable std::wstring m_tooltip_cache;
    HICON m_h_icon = nullptr;
};
