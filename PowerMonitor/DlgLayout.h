// DlgLayout.h : 对话框标签布局辅助
//
// 背景：早期版本的标签宽度按对话框单位(DLU)硬编码写死（如 62 DLU），
// 但中文标签比英文宽、且宽度随 DPI 与系统字号变化，实测出现标签文字
// 超出控件矩形的情况——标签是右对齐(SS_RIGHT)，文字从左侧溢出，
// 表现为"输入框都在、左侧字段名全部消失"。
//
// 这里的原则：**标签列宽由实际字体测量决定，而不是猜一个 DLU 数字**。
// 这样对中文/英文/其他语言长度、以及任意 DPI 都自适应。
#pragma once

#include <vector>

namespace dlglayout {

// 用指定字体测量单行文字需要的像素宽度
int MeasureTextWidth(HDC dc, HFONT font, const wchar_t* text);

// 用控件的当前字体测量其文字需要的像素宽度（无 HWND 时返回 0）
int MeasureWindowTextWidth(HWND hwnd);

// 在对话框上测一行文字所需的 DLU 宽度（内部会换算像素/字符宽比）
// hDlg  : 对话框窗口
// font  : 用于测量的字体
// text  : 待测文字
// 返回  : 需要的宽度，单位 DLU（已含内边距）
int MeasureTextDlu(HWND hDlg, HFONT font, const wchar_t* text);

// 给一组标签算出统一的列宽（DLU），返回其中最大者 + 内边距。
// 统一列宽能让同一组内所有输入框左边缘对齐，保持视觉整齐。
int MeasureLabelColumnDlu(HWND hDlg, HFONT font,
                          const wchar_t* const* labels, int count,
                          int min_dlu, int pad_dlu);

// 判断控件文字是否放得下（供自检使用）
bool TextFits(HWND hwnd);

// 用控件自身的字体实测文字需要多宽（像素）。与自检探针同一口径，
// 因此"按它调整控件宽度"必然能通过自检——不受 DLU/DPI 换算误差影响。
int MeasureSelfTextWidth(HWND hwnd);

// 把控件加宽到刚好放下其文字：右边缘不动，向左扩展（SS_RIGHT 标签
// 的文字是从左侧溢出的，所以必须往左长）。
// 返回实际需要的像素宽度（未做任何改动时也返回测量值）。
int WidenToFitText(HWND hwnd);

// 把长文本按控件宽度自动折行，返回折行后的文本。
// 用于"来源信息"这类需要多行完整显示的说明文字：
// 高 DPI 或长文案时，SS_LEFT 不自动换行会直接截断。
// 中文无空格，必须支持逐字断行，否则整段会被当成一个"单词"永不换行。
std::wstring WrapToWidth(HWND hwnd, HFONT font, const std::wstring& text, int width_px, int max_lines);

// 计算折行后需要的像素高度
int MeasureWrappedHeight(HWND hwnd, HFONT font, const std::wstring& wrapped, int width_px);

} // namespace dlglayout
