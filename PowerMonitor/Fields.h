// Fields.h : 插件 15 个显示字段的定义与取值
// 跨平台纯逻辑，文本为 UTF-8。
#pragma once

#include "Meter.h"
#include <string>
#include <vector>

struct FieldDef
{
    std::string key;
    std::string label;        // 正式标签
    std::string short_label;  // 短标签
};

// 15 个字段，顺序即定义顺序
const std::vector<FieldDef>& fieldDefs();

// 字段标签（UTF-8）
std::string fieldLabel(const std::string& key);

// 字段数值文本（含单位 / 货币符号，UTF-8）
std::string fieldValueText(const std::string& key, const Snapshot& s, const Config& cfg);

// 字段数值样本（用于主程序计算显示宽度，UTF-8）
std::string fieldSample(const std::string& key, const Config& cfg);
