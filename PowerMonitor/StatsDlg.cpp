// StatsDlg.cpp : 用量统计窗口实现
#include "pch.h"
#include "StatsDlg.h"
#include "Tariffs.h"
#include "Encoding.h"
#include <cstdio>

IMPLEMENT_DYNAMIC(CStatsDlg, CDialogEx)

BEGIN_MESSAGE_MAP(CStatsDlg, CDialogEx)
    ON_NOTIFY(TCN_SELCHANGE, IDC_STATS_TAB, &CStatsDlg::OnTabChanged)
END_MESSAGE_MAP()

CStatsDlg::CStatsDlg(EnergyMeter& meter, CWnd* parent)
    : CDialogEx(IDD, parent), m_meter(meter)
{
}

CStatsDlg::~CStatsDlg()
{
}

static CString energyText(double wh)
{
    CString s;
    if (wh >= 1000.0)
        s.Format(L"%.2f kWh", wh / 1000.0);
    else
        s.Format(L"%.0f Wh", wh);
    return s;
}

static CString costText(const Config& cfg, double cost)
{
    std::wstring cur = Utf8ToWString(cfg.currency);
    CString s;
    s.Format(L"%s%.2f", cur.c_str(), cost);
    return s;
}

// 对话框单位 -> 像素（随系统 DPI / 字体度量自动换算）
static CRect Dlu(HWND hDlg, int l, int t, int r, int b)
{
    CRect rc(l, t, r, b);
    ::MapDialogRect(hDlg, &rc);
    return rc;
}

BOOL CStatsDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    HWND h = GetSafeHwnd();

    // 客户区 = 模板尺寸（IDD_STATS = 280x255 DLU）。
    // 模板里的"关闭"按钮在 y=237..253 DLU，客户区必须按 DLU 换算，
    // 否则固定像素尺寸在非 100% DPI 下会把按钮裁到客户区之外。
    CRect full = Dlu(h, 0, 0, 280, 255);
    RECT wr = { 0, 0, full.right - full.left, full.bottom - full.top };
    AdjustWindowRectEx(&wr, GetStyle(), FALSE, GetExStyle());
    SetWindowPos(nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    m_font.CreatePointFont(90, L"MS Shell Dlg");

    SYSTEMTIME lt;
    ::GetLocalTime(&lt);
    m_month = lt.wMonth;

    // Tab
    m_tab.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FOCUSNEVER,
                 Dlu(h, 5, 6, 275, 24), this, IDC_STATS_TAB);
    m_tab.SetFont(&m_font);
    const wchar_t* tabs[5] = { L"按天", L"按月", L"按年", L"每次开机", L"时段" };
    for (int i = 0; i < 5; ++i)
        m_tab.InsertItem(i, tabs[i]);
    m_tab.SetCurSel(0);

    // List
    m_list.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP
                  | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER
                  | LVS_ALIGNLEFT,
                  Dlu(h, 5, 27, 275, 205), this, IDC_STATS_LIST);
    m_list.SetFont(&m_font);
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Summary
    m_summary.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                     Dlu(h, 6, 209, 274, 224), this, IDC_STATS_SUMMARY);
    m_summary.SetFont(&m_font);

    refreshList();
    return TRUE;
}

void CStatsDlg::OnTabChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    (void)pNMHDR;
    refreshList();
    *pResult = 0;
}

void CStatsDlg::refreshList()
{
    static const char* kinds[5] = { "day", "month", "year", "session", "hour" };
    static const wchar_t* col0[5] = { L"日期", L"月份", L"年份", L"开机时间", L"时段" };
    static const wchar_t* col3_t[5] = { L"备注 / 时长", L"天数 · 日均", L"天数 · 日均", L"开机时长", L"占比" };

    int tab = m_tab.GetCurSel();
    if (tab < 0)
        tab = 0;
    const char* kind = kinds[tab];
    const Config& cfg = m_meter.config();

    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();
    while (m_list.DeleteColumn(0))
    {
    }

    m_list.InsertColumn(0, col0[tab], LVCFMT_LEFT, 150);
    m_list.InsertColumn(1, L"电量", LVCFMT_RIGHT, 90);
    m_list.InsertColumn(2, L"电费", LVCFMT_RIGHT, 80);
    m_list.InsertColumn(3, col3_t[tab], LVCFMT_LEFT, 86);

    std::vector<StatsRow> rows = m_meter.statsRows(kind, m_month);

    double sum_wh = 0, sum_cost = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const StatsRow& r = rows[i];
        CString when = Utf8ToCString(r.when);
        int row_idx = m_list.InsertItem((int)i, when);
        m_list.SetItemText(row_idx, 1, energyText(r.wh));
        m_list.SetItemText(row_idx, 2, costText(cfg, r.cost));

        CString note = Utf8ToCString(r.note);
        if (std::string(kind) == "day" && r.seconds > 0)
        {
            CString extra = Utf8ToCString(compactDuration(r.seconds));
            note = note + L" · " + extra;
        }
        m_list.SetItemText(row_idx, 3, note);

        sum_wh += r.wh;
        sum_cost += r.cost;
    }

    CString summary;
    summary.Format(L"合计 %s    电费 %s    %d 条记录",
                   energyText(sum_wh).GetString(),
                   costText(cfg, sum_cost).GetString(),
                   (int)rows.size());
    m_summary.SetWindowText(summary);

    m_list.SetRedraw(TRUE);
}
