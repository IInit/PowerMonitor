// PowerMonitorItem.cpp : 插件显示项实现
#include "pch.h"
#include "PowerMonitorItem.h"
#include "PowerMonitor.h"
#include "Encoding.h"

CPowerMonitorItem::CPowerMonitorItem(CPowerMonitor* owner, const std::string& key)
    : m_owner(owner), m_key(key), m_id(Utf8ToWString(key))
{
}

const wchar_t* CPowerMonitorItem::GetItemName() const
{
    m_name = Utf8ToWString(m_owner->itemFullLabelUtf8(m_key));
    return m_name.c_str();
}

const wchar_t* CPowerMonitorItem::GetItemId() const
{
    return m_id.c_str();
}

const wchar_t* CPowerMonitorItem::GetItemLableText() const
{
    m_label = Utf8ToWString(m_owner->itemShortLabelUtf8(m_key));
    return m_label.c_str();
}

const wchar_t* CPowerMonitorItem::GetItemValueText() const
{
    m_value = Utf8ToWString(m_owner->itemValueUtf8(m_key));
    return m_value.c_str();
}

const wchar_t* CPowerMonitorItem::GetItemValueSampleText() const
{
    m_sample = Utf8ToWString(m_owner->itemSampleUtf8(m_key));
    return m_sample.c_str();
}
