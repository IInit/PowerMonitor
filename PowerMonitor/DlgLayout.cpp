// DlgLayout.cpp : 对话框标签布局辅助实现
#include "pch.h"
#include "DlgLayout.h"

#include <algorithm>

namespace dlglayout {

int MeasureTextWidth(HDC dc, HFONT font, const wchar_t* text)
{
    if (dc == nullptr || text == nullptr || *text == 0)
        return 0;

    HGDIOBJ old = nullptr;
    if (font != nullptr)
        old = ::SelectObject(dc, font);

    SIZE sz{ 0, 0 };
    // GetTextExtentPoint32W 对单行文字最直接；比 DrawText 少一层格式化开销
    ::GetTextExtentPoint32W(dc, text, (int)::wcslen(text), &sz);

    if (old != nullptr)
        ::SelectObject(dc, old);
    return sz.cx;
}

int MeasureWindowTextWidth(HWND hwnd)
{
    if (hwnd == nullptr)
        return 0;

    int n = ::GetWindowTextLengthW(hwnd);
    if (n <= 0)
        return 0;

    std::wstring s((size_t)n, L'\0');
    ::GetWindowTextW(hwnd, &s[0], n + 1);

    HDC dc = ::GetDC(hwnd);
    if (dc == nullptr)
        return 0;

    HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
    int w = MeasureTextWidth(dc, font, s.c_str());
    ::ReleaseDC(hwnd, dc);
    return w;
}

// 对话框单位换算：MapDialogRect 把 DLU 矩形换成像素。
// 反向换算需要知道"1 DLU = 多少像素"，直接用一个基准矩形量出来。
static int DluToPx(HWND hDlg, int dlu)
{
    CRect rc(0, 0, dlu, dlu);
    ::MapDialogRect(hDlg, &rc);
    return rc.right - rc.left;
}

int MeasureTextDlu(HWND hDlg, HFONT font, const wchar_t* text)
{
    if (hDlg == nullptr || text == nullptr || *text == 0)
        return 0;

    HDC dc = ::GetDC(hDlg);
    if (dc == nullptr)
        return 0;

    int need_px = MeasureTextWidth(dc, font, text);
    ::ReleaseDC(hDlg, dc);

    // 水平方向：1 DLU == 对话框字体平均字符宽 / 4，
    // 用 MapDialogRect 实测出比例，避免自己假设字体尺寸。
    int px_per_100dlu = DluToPx(hDlg, 100);
    if (px_per_100dlu <= 0)
        px_per_100dlu = 100;

    return (need_px * 100 + px_per_100dlu - 1) / px_per_100dlu;   // 向上取整
}

int MeasureLabelColumnDlu(HWND hDlg, HFONT font,
                          const wchar_t* const* labels, int count,
                          int min_dlu, int pad_dlu)
{
    int widest = min_dlu;   // 下限：太窄会显得拥挤，且给英文标签留余量
    for (int i = 0; i < count; ++i)
    {
        int w = MeasureTextDlu(hDlg, font, labels[i]);
        if (w > widest)
            widest = w;
    }
    return widest + pad_dlu;
}

bool TextFits(HWND hwnd)
{
    if (hwnd == nullptr)
        return true;

    int n = ::GetWindowTextLengthW(hwnd);
    if (n <= 0)
        return true;

    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    int have = rc.right - rc.left;
    if (have <= 0)
        return true;

    return MeasureWindowTextWidth(hwnd) <= have;
}

int MeasureSelfTextWidth(HWND hwnd)
{
    // 与 dialog_probe 的 MeasureTextFit 完全同口径：控件自己的 DC + 自己的字体
    if (hwnd == nullptr)
        return 0;

    std::wstring t;
    int n = ::GetWindowTextLengthW(hwnd);
    if (n > 0)
    {
        t.resize((size_t)n);
        ::GetWindowTextW(hwnd, &t[0], n + 1);
    }
    if (t.empty())
        return 0;

    HDC dc = ::GetDC(hwnd);
    if (dc == nullptr)
        return 0;

    HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
    HGDIOBJ old = nullptr;
    if (font != nullptr)
        old = ::SelectObject(dc, font);

    RECT calc{ 0, 0, 0, 0 };
    ::DrawTextW(dc, t.c_str(), (int)t.size(), &calc,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    int need = calc.right - calc.left;

    if (old != nullptr)
        ::SelectObject(dc, old);
    ::ReleaseDC(hwnd, dc);
    return need;
}

int WidenToFitText(HWND hwnd)
{
    if (hwnd == nullptr)
        return 0;

    int need = MeasureSelfTextWidth(hwnd);
    if (need <= 0)
        return 0;

    RECT wr{};
    ::GetWindowRect(hwnd, &wr);
    HWND parent = ::GetParent(hwnd);
    POINT tl{ wr.left, wr.top };
    POINT br{ wr.right, wr.bottom };
    if (parent)
    {
        ::ScreenToClient(parent, &tl);
        ::ScreenToClient(parent, &br);
    }

    int have = br.x - tl.x;

#ifdef PM_DLGL_DEBUG
    {
        wchar_t t[128] = { 0 };
        ::GetWindowTextW(hwnd, t, 127);

        // 对照测量：控件自身字体 vs 父对话框字体 vs 显式 9pt 字体
        HFONT f_self = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
        HFONT f_dlg = (HFONT)::SendMessageW(::GetParent(hwnd), WM_GETFONT, 0, 0);
        int n_self = 0, n_dlg = 0;
        {
            HDC dc = ::GetDC(hwnd);
            LOGFONTW lf{};
            if (f_self) { ::GetObjectW(f_self, sizeof(lf), &lf); }
            n_self = 0;
            if (dc)
            {
                HGDIOBJ o = f_self ? ::SelectObject(dc, f_self) : nullptr;
                RECT c{ 0, 0, 0, 0 };
                ::DrawTextW(dc, t, -1, &c, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                n_self = c.right - c.left;
                if (o) ::SelectObject(dc, o);
                ::ReleaseDC(hwnd, dc);
            }
            if (f_dlg && f_dlg != f_self)
            {
                HDC dc2 = ::GetDC(hwnd);
                if (dc2)
                {
                    HGDIOBJ o = ::SelectObject(dc2, f_dlg);
                    RECT c{ 0, 0, 0, 0 };
                    ::DrawTextW(dc2, t, -1, &c, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                    n_dlg = c.right - c.left;
                    ::SelectObject(dc2, o);
                    ::ReleaseDC(hwnd, dc2);
                }
            }
            wchar_t buf[768];
            ::wsprintfW(buf, L"[DLGL] self=%d dlg=%d hSelf=%d hDlg=%d wSelf=%d need=%d have=%d text=%s\r\n",
                        n_self, n_dlg, (int)(INT_PTR)f_self, (int)(INT_PTR)f_dlg,
                        lf.lfHeight, need, have, t);
            HANDLE f = ::CreateFileW(L"D:\\_dlgl_layout.log", FILE_APPEND_DATA, FILE_SHARE_READ,
                                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                ::WriteFile(f, buf, (DWORD)(::wcslen(buf) * sizeof(wchar_t)), &written, nullptr);
                ::CloseHandle(f);
            }
        }
    }
#endif

    if (need <= have)
        return need;   // 已经放得下

    // 右边缘保持不动，向左扩展：SS_RIGHT/SS_CENTER 标签都是朝左侧溢出的
    int new_left = br.x - need;
    ::SetWindowPos(hwnd, nullptr, new_left, tl.y, need, br.y - tl.y,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    return need;
}

// ---------------------------------------------------------------- 自动折行
// 按像素宽度贪心折行，中英文混排都能处理：
//   · 显式换行符 \n 直接断行
//   · 英文单词尽量整体移行（优先在空格处断）
//   · 中文没有空格，必须支持**逐字断行**，否则整段会被当成一个单词永不换行
// 这是最早版本的致命 bug：扫描断点时一路扫到字符串结尾，导致中文长文本
// 完全不换行、直接被控件右边缘裁掉。
std::wstring WrapToWidth(HWND hwnd, HFONT font, const std::wstring& text,
                         int width_px, int max_lines)
{
    if (text.empty() || width_px <= 0)
        return text;

    HDC dc = ::GetDC(hwnd);
    if (dc == nullptr)
        return text;

    HGDIOBJ old = nullptr;
    if (font != nullptr)
        old = ::SelectObject(dc, font);

    auto width_of = [&](const std::wstring& s) -> int {
        if (s.empty())
            return 0;
        SIZE sz{ 0, 0 };
        ::GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
        return sz.cx;
    };

    // 先按显式换行切成"逻辑段"，再对每段做自动折行
    std::vector<std::wstring> paras;
    {
        std::wstring cur;
        for (wchar_t c : text)
        {
            if (c == L'\n')
            {
                paras.push_back(cur);
                cur.clear();
            }
            else if (c != L'\r')
            {
                cur.push_back(c);
            }
        }
        paras.push_back(cur);
    }

    std::wstring out;
    int lines = 0;

    for (size_t p = 0; p < paras.size(); ++p)
    {
        const std::wstring& para = paras[p];
        if (lines >= max_lines)
            break;
        if (p > 0)
        {
            out += L'\n';
        }

        std::wstring line;
        size_t i = 0;
        while (i < para.size() && lines < max_lines)
        {
            // 试着把 [i, j) 放进当前行。先找"下一个可能的断点"：
            //   - 英文：空格处（单词整体移动）
            //   - 中文/其他：单字（逐字断行）
            size_t j = i;
            bool cjk = false;
            size_t word_end = i;
            while (word_end < para.size() && para[word_end] != L' ')
            {
                // 只要遇到非 ASCII（CJK、全角标点等）就按单字断
                if ((unsigned)para[word_end] >= 0x2E80)
                {
                    cjk = true;
                    break;
                }
                ++word_end;
            }
            if (cjk)
                j = i + 1;              // 中文：一次只取一个字
            else
                j = (word_end > i) ? word_end : i + 1;   // 英文：取整个单词

            std::wstring cand = line + para.substr(i, j - i);
            if (!line.empty() && width_of(cand) > width_px)
            {
                // 放不下：当前行成行，本段留在新行重试（不推进 i）。
                // 注意必须先把 line 落到 out —— 漏掉这步会静默丢字。
                out += line;
                out += L'\n';
                ++lines;
                line.clear();
                continue;
            }

            line += para.substr(i, j - i);
            i = j;

            // 英文单词后的空格：能跟就跟，跟不下就换行
            if (i < para.size() && para[i] == L' ')
            {
                if (width_of(line + L" ") <= width_px)
                    line += L' ';
                else
                {
                    out += line;   // 同上：先落盘，再换行
                    out += L'\n';
                    ++lines;
                    line.clear();
                }
                ++i;
            }
        }

        out += line;
        ++lines;
        if (lines >= max_lines && p + 1 < paras.size())
            break;
    }

    if (old != nullptr)
        ::SelectObject(dc, old);
    ::ReleaseDC(hwnd, dc);
    return out;
}

int MeasureWrappedHeight(HWND hwnd, HFONT font, const std::wstring& wrapped, int width_px)
{
    if (wrapped.empty() || width_px <= 0)
        return 0;

    HDC dc = ::GetDC(hwnd);
    if (dc == nullptr)
        return 0;

    HGDIOBJ old = nullptr;
    if (font != nullptr)
        old = ::SelectObject(dc, font);

    RECT rc{ 0, 0, width_px, 0 };
    ::DrawTextW(dc, wrapped.c_str(), (int)wrapped.size(), &rc,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int h = rc.bottom - rc.top;

    if (old != nullptr)
        ::SelectObject(dc, old);
    ::ReleaseDC(hwnd, dc);
    return h;
}

} // namespace dlglayout
