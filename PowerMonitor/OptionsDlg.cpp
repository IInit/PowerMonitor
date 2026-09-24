// OptionsDlg.cpp : 硬件模型 / 采样 / 校准设置实现
//
// 布局约定同 TariffDlg：坐标一律用对话框单位(DLU)，客户区尺寸由模板 DLU 换算，
// 不用固定像素尺寸（否则高 DPI 下模板按钮会被裁掉）。
#include "pch.h"
#include "OptionsDlg.h"
#include <cstdio>

IMPLEMENT_DYNAMIC(COptionsDlg, CDialogEx)

COptionsDlg::COptionsDlg(const Config& cfg, CWnd* parent)
    : CDialogEx(IDD, parent), m_cfg(cfg)
{
}

COptionsDlg::~COptionsDlg()
{
}

// 对话框单位 -> 像素
static CRect Dlu(HWND hDlg, int l, int t, int r, int b)
{
    CRect rc(l, t, r, b);
    ::MapDialogRect(hDlg, &rc);
    return rc;
}

static CString num(double v, const char* fmt)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return CString(buf);
}

CStatic* COptionsDlg::createPlainLabel(const CString& text, int x, int y, int w, int h)
{
    CStatic* s = new CStatic();
    s->Create(text, WS_CHILD | WS_VISIBLE | SS_LEFT, Dlu(GetSafeHwnd(), x, y, x + w, y + h), this);
    s->SetFont(&m_font);
    return s;
}

// x / y / label_w / edit_w 均为对话框单位
void COptionsDlg::createRow(CStatic& label, CEdit& edit, const CString& text,
                            int x, int y, int label_w, int edit_w, UINT nid)
{
    HWND h = GetSafeHwnd();
    label.Create(text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                 Dlu(h, x, y, x + label_w, y + 11), this);
    label.SetFont(&m_font);
    label.Detach();   // 标签为静态展示控件，脱离局部对象，随对话框自动销毁
    edit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                Dlu(h, x + label_w + 4, y, x + label_w + 4 + edit_w, y + 11), this, nid);
    edit.SetFont(&m_font);
}

BOOL COptionsDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    HWND h = GetSafeHwnd();

    // 客户区 = 模板尺寸（IDD_OPTIONS = 210x190 DLU）
    CRect full = Dlu(h, 0, 0, 210, 190);
    RECT wr = { 0, 0, full.right - full.left, full.bottom - full.top };
    AdjustWindowRectEx(&wr, GetStyle(), FALSE, GetExStyle());
    SetWindowPos(nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    m_font.CreatePointFont(90, L"MS Shell Dlg");

    // ---- 硬件功耗模型 ----
    CStatic grp_hw;
    grp_hw.Create(L"硬件功耗模型", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, Dlu(h, 5, 4, 205, 80), this);
    grp_hw.SetFont(&m_font);
    grp_hw.Detach();

    CStatic l_ppt, l_idle, l_exp, l_base;
    createRow(l_ppt, m_e_ppt, L"CPU 封装功耗上限 (W)", 10, 12, 62, 50, IDC_OPT_CPU_PPT);
    m_e_ppt.SetWindowText(num(m_cfg.cpu_ppt, "%.1f"));
    m_s_ppt.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                   Dlu(h, 127, 12, 137, 23), this, IDC_OPT_SPIN_PPT);
    m_s_ppt.SetRange(10, 500);
    m_s_ppt.SetBuddy(&m_e_ppt);
    m_s_ppt.SetPos((int)m_cfg.cpu_ppt);

    createRow(l_idle, m_e_idle, L"CPU 空载功耗 (W)", 10, 27, 62, 50, IDC_OPT_CPU_IDLE);
    m_e_idle.SetWindowText(num(m_cfg.cpu_idle, "%.1f"));
    m_s_idle.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                    Dlu(h, 127, 27, 137, 38), this, IDC_OPT_SPIN_IDLE);
    m_s_idle.SetRange(0, 200);
    m_s_idle.SetBuddy(&m_e_idle);
    m_s_idle.SetPos((int)m_cfg.cpu_idle);

    createRow(l_exp, m_e_exp, L"负载-功耗曲线指数", 10, 42, 62, 50, IDC_OPT_CPU_EXP);
    m_e_exp.SetWindowText(num(m_cfg.cpu_load_exponent, "%.2f"));

    createRow(l_base, m_e_baseline, L"其他固定开销 (W)", 10, 57, 62, 50, IDC_OPT_BASELINE);
    m_e_baseline.SetWindowText(num(m_cfg.baseline_watts, "%.1f"));
    m_s_baseline.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                        Dlu(h, 127, 57, 137, 68), this, IDC_OPT_SPIN_BASELINE);
    m_s_baseline.SetRange(0, 200);
    m_s_baseline.SetBuddy(&m_e_baseline);
    m_s_baseline.SetPos((int)m_cfg.baseline_watts);

    // ---- 采样与校准 ----
    // 注意：不要用 grp2 / grp3 作变量名，Windows dlgs.h 里定义了同名宏（grp2=0x0431）
    CStatic grp_sample;
    grp_sample.Create(L"采样与校准", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, Dlu(h, 5, 84, 205, 128), this);
    grp_sample.SetFont(&m_font);
    grp_sample.Detach();

    CStatic l_interval, l_calib;
    createRow(l_interval, m_e_interval, L"期望采样间隔 (秒)", 10, 92, 62, 50, IDC_OPT_INTERVAL);
    m_e_interval.SetWindowText(num(m_cfg.sample_interval, "%.1f"));
    createRow(l_calib, m_e_calib, L"整机校准系数", 10, 108, 62, 50, IDC_OPT_CALIBRATION);
    m_e_calib.SetWindowText(num(m_cfg.calibration, "%.3f"));

    // ---- 整机口径 ----
    CStatic grp_scope;
    grp_scope.Create(L"整机口径", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, Dlu(h, 5, 132, 205, 168), this);
    grp_scope.SetFont(&m_font);
    grp_scope.Detach();

    m_chk_monitor.Create(L"把显示器功耗计入整机", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                         Dlu(h, 10, 138, 150, 149), this, IDC_OPT_INC_MONITOR);
    m_chk_monitor.SetFont(&m_font);
    m_chk_monitor.SetCheck(m_cfg.include_monitor ? BST_CHECKED : BST_UNCHECKED);

    m_lbl_monitor.Create(L"显示器功耗 (W)", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                         Dlu(h, 10, 152, 72, 163), this);
    m_lbl_monitor.SetFont(&m_font);
    m_e_monitor.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                       Dlu(h, 76, 152, 126, 163), this, IDC_OPT_MONITOR_W);
    m_e_monitor.SetFont(&m_font);
    m_e_monitor.SetWindowText(num(m_cfg.monitor_watts, "%.1f"));
    m_s_monitor.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                       Dlu(h, 127, 152, 137, 163), this, IDC_OPT_SPIN_MONITOR);
    m_s_monitor.SetRange(0, 200);
    m_s_monitor.SetBuddy(&m_e_monitor);
    m_s_monitor.SetPos((int)m_cfg.monitor_watts);

    return TRUE;
}

static double readDouble(CEdit& e, double fallback)
{
    if (!e.GetSafeHwnd())
        return fallback;
    CString s;
    e.GetWindowText(s);
    s.Trim();
    if (s.IsEmpty())
        return fallback;
    const wchar_t* begin = s.GetString();
    wchar_t* end = nullptr;
    double v = wcstod(begin, &end);
    if (end == begin)
        return fallback;
    return v;
}

void COptionsDlg::OnOK()
{
    m_cfg.cpu_ppt = readDouble(m_e_ppt, m_cfg.cpu_ppt);
    m_cfg.cpu_idle = readDouble(m_e_idle, m_cfg.cpu_idle);
    m_cfg.cpu_load_exponent = readDouble(m_e_exp, m_cfg.cpu_load_exponent);
    m_cfg.baseline_watts = readDouble(m_e_baseline, m_cfg.baseline_watts);
    m_cfg.sample_interval = readDouble(m_e_interval, m_cfg.sample_interval);
    m_cfg.calibration = readDouble(m_e_calib, m_cfg.calibration);
    m_cfg.include_monitor = m_chk_monitor.GetCheck() == BST_CHECKED;
    m_cfg.monitor_watts = readDouble(m_e_monitor, m_cfg.monitor_watts);

    // 合理范围约束
    if (m_cfg.cpu_ppt < 1) m_cfg.cpu_ppt = 1;
    if (m_cfg.cpu_idle < 0) m_cfg.cpu_idle = 0;
    if (m_cfg.cpu_idle > m_cfg.cpu_ppt) m_cfg.cpu_idle = m_cfg.cpu_ppt;
    if (m_cfg.cpu_load_exponent < 0.1) m_cfg.cpu_load_exponent = 0.1;
    if (m_cfg.cpu_load_exponent > 3) m_cfg.cpu_load_exponent = 3;
    if (m_cfg.baseline_watts < 0) m_cfg.baseline_watts = 0;
    if (m_cfg.sample_interval < 0.5) m_cfg.sample_interval = 0.5;
    if (m_cfg.calibration <= 0) m_cfg.calibration = 1.0;
    if (m_cfg.monitor_watts < 0) m_cfg.monitor_watts = 0;

    CDialogEx::OnOK();
}
