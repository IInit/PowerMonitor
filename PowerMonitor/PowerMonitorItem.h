// PowerMonitorItem.h : 插件显示项（非自绘，主程序绘制 标签 + 数值）
#pragma once

#include "include/PluginInterface.h"
#include <string>

class CPowerMonitor;

class CPowerMonitorItem : public IPluginItem
{
public:
    CPowerMonitorItem(CPowerMonitor* owner, const std::string& key);
    virtual ~CPowerMonitorItem() = default;

    // IPluginItem
    virtual const wchar_t* GetItemName() const override;          // 显示项名称（选择项列表）
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;     // 数值前的短标签
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override { return false; }

    const std::string& Key() const { return m_key; }

private:
    CPowerMonitor* m_owner;
    std::string m_key;
    std::wstring m_id;
    mutable std::wstring m_name;
    mutable std::wstring m_label;
    mutable std::wstring m_value;
    mutable std::wstring m_sample;
};
