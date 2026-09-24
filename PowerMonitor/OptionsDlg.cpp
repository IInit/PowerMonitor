// OptionsDlg.cpp : 硬件模型 / 采样 / 校准设置实现
//
// 布局约定同 TariffDlg：坐标一律用对话框单位(DLU)，客户区尺寸由模板 DLU 换算，
// 不用固定像素尺寸（否则高 DPI 下模板按钮会被裁掉）。
#include "pch.h"
#include "OptionsDlg.h"
#include "DlgLayout.h"
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
    // 标签高度按行高给足（11 DLU 在部分字体/DPI 下会压到文字下缘），
    // 宽度由调用方按实际字体测量后传入，不再写死。
    label.Create(text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                 Dlu(h, x, y, x + label_w, y + 13), this);
    label.SetFont(&m_font);
    // 兜底自校正：创建后用控件自身的字体与 DC 再测一次，若仍放不下就向左加宽，
    // 并把输入框右移同样的量。DLU↔像素的换算在不同 DPI/字体下会有偏差，
    // 这一步保证"最终屏幕上的实际渲染结果"一定放得下，而不只是"算出来放得下"。
    int need_px = dlglayout::MeasureSelfTextWidth(label.GetSafeHwnd());
    if (need_px > 0)
    {
        CRect lr;
        label.GetWindowRect(&lr);
        ScreenToClient(&lr);
        if (need_px > lr.Width())
            dlglayout::WidenToFitText(label.GetSafeHwnd());
    }
    // 注意：绝不要 Detach()。Detach 会让控件字体丢失，渲染回退到系统默认字体，
    // 文字比测量值宽约 23%，右对齐标签会从左侧被裁掉。标签一律用持久成员承载。
    edit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                Dlu(h, x + label_w + 4, y, x + label_w + 4 + edit_w, y + 13), this, nid);
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

    // 标签列宽由实际字体测量决定，而不是写死 DLU——
    // 中文标签比英文宽，写死会导致右对齐标签从左侧溢出被完全裁掉
    // （曾表现为"左侧字段名全部消失，只剩输入框"）。
    // 两组的标签集合不同，分别测量后取各自需求的较大值，保证组内左边缘对齐。
    static const wchar_t* kHwLabels[] = {
        L"CPU 封装功耗上限 (W)", L"CPU 空载功耗 (W)",
        L"负载-功耗曲线指数", L"其他固定开销 (W)",
    };
    static const wchar_t* kSampleLabels[] = {
        L"期望采样间隔 (秒)", L"整机校准系数",
    };
    const int kLabelMinDlu = 62;    // 下限：与历史版视觉一致，同时容纳最短的英文标签
    const int kLabelPadDlu = 6;     // 标签与输入框之间的间隔
    int label_hw = dlglayout::MeasureLabelColumnDlu(
        h, (HFONT)m_font.GetSafeHandle(), kHwLabels, 4, kLabelMinDlu, kLabelPadDlu);
    int label_sample = dlglayout::MeasureLabelColumnDlu(
        h, (HFONT)m_font.GetSafeHandle(), kSampleLabels, 2, kLabelMinDlu, kLabelPadDlu);
    // 整机口径区的「显示器功耗 (W)」也参与对齐
    int label_monitor = dlglayout::MeasureLabelColumnDlu(
        h, (HFONT)m_font.GetSafeHandle(), &kSampleLabels[0], 0, kLabelMinDlu, kLabelPadDlu);

    // 客户区宽度固定为模板宽度，输入框宽度随列宽自适应：
    // 先算出标签列右边界，再让输入框吃掉剩余空间（右侧留出微调按钮的位置）。
    const int kDlgW = 210;
    const int kEditRight = 131;         // 输入框右边界（DLU），右侧 131..137 留给 spin
    auto editWidth = [&](int label_w) { return kEditRight - (10 + label_w + 4); };

    // ---- 硬件功耗模型 ----
    m_grp_hw.Create(L"硬件功耗模型", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    Dlu(h, 5, 4, kDlgW - 5, 80), this, -1);
    m_grp_hw.SetFont(&m_font);

    createRow(m_lbl_ppt, m_e_ppt, L"CPU 封装功耗上限 (W)", 10, 12, label_hw, editWidth(label_hw), IDC_OPT_CPU_PPT);
    m_e_ppt.SetWindowText(num(m_cfg.cpu_ppt, "%.1f"));
    m_s_ppt.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                   Dlu(h, kEditRight, 12, kEditRight + 10, 25), this, IDC_OPT_SPIN_PPT);
    m_s_ppt.SetRange(10, 500);
    m_s_ppt.SetBuddy(&m_e_ppt);
    m_s_ppt.SetPos((int)m_cfg.cpu_ppt);

    createRow(m_lbl_idle, m_e_idle, L"CPU 空载功耗 (W)", 10, 27, label_hw, editWidth(label_hw), IDC_OPT_CPU_IDLE);
    m_e_idle.SetWindowText(num(m_cfg.cpu_idle, "%.1f"));
    m_s_idle.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                    Dlu(h, kEditRight, 27, kEditRight + 10, 40), this, IDC_OPT_SPIN_IDLE);
    m_s_idle.SetRange(0, 200);
    m_s_idle.SetBuddy(&m_e_idle);
    m_s_idle.SetPos((int)m_cfg.cpu_idle);

    createRow(m_lbl_exp, m_e_exp, L"负载-功耗曲线指数", 10, 42, label_hw, editWidth(label_hw), IDC_OPT_CPU_EXP);
    m_e_exp.SetWindowText(num(m_cfg.cpu_load_exponent, "%.2f"));

    createRow(m_lbl_baseline, m_e_baseline, L"其他固定开销 (W)", 10, 57, label_hw, editWidth(label_hw), IDC_OPT_BASELINE);
    m_e_baseline.SetWindowText(num(m_cfg.baseline_watts, "%.1f"));
    m_s_baseline.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                        Dlu(h, kEditRight, 57, kEditRight + 10, 70), this, IDC_OPT_SPIN_BASELINE);
    m_s_baseline.SetRange(0, 200);
    m_s_baseline.SetBuddy(&m_e_baseline);
    m_s_baseline.SetPos((int)m_cfg.baseline_watts);

    // ---- 采样与校准 ----
    // 注意：不要用 grp2 / grp3 作变量名，Windows dlgs.h 里定义了同名宏（grp2=0x0431）
    m_grp_sample.Create(L"采样与校准", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                        Dlu(h, 5, 84, kDlgW - 5, 128), this, -1);
    m_grp_sample.SetFont(&m_font);

    createRow(m_lbl_interval, m_e_interval, L"期望采样间隔 (秒)", 10, 92, label_sample, editWidth(label_sample), IDC_OPT_INTERVAL);
    m_e_interval.SetWindowText(num(m_cfg.sample_interval, "%.1f"));
    createRow(m_lbl_calib, m_e_calib, L"整机校准系数", 10, 108, label_sample, editWidth(label_sample), IDC_OPT_CALIBRATION);
    m_e_calib.SetWindowText(num(m_cfg.calibration, "%.3f"));

    // ---- 整机口径 ----
    m_grp_scope.Create(L"整机口径", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                       Dlu(h, 5, 132, kDlgW - 5, 168), this, -1);
    m_grp_scope.SetFont(&m_font);

    m_chk_monitor.Create(L"把显示器功耗计入整机", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                         Dlu(h, 10, 138, 155, 151), this, IDC_OPT_INC_MONITOR);
    m_chk_monitor.SetFont(&m_font);
    m_chk_monitor.SetCheck(m_cfg.include_monitor ? BST_CHECKED : BST_UNCHECKED);

    m_lbl_monitor.Create(L"显示器功耗 (W)", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                         Dlu(h, 10, 152, 10 + label_monitor, 165), this);
    m_lbl_monitor.SetFont(&m_font);
    // 同 createRow：创建后再用控件自身字体自校正一次（见 createRow 内注释）
    if (dlglayout::MeasureSelfTextWidth(m_lbl_monitor.GetSafeHwnd()) > 0)
        dlglayout::WidenToFitText(m_lbl_monitor.GetSafeHwnd());
    int mon_edit_x = 10 + label_monitor + 4;
    m_e_monitor.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                       Dlu(h, mon_edit_x, 152, kEditRight, 165), this, IDC_OPT_MONITOR_W);
    m_e_monitor.SetFont(&m_font);
    m_e_monitor.SetWindowText(num(m_cfg.monitor_watts, "%.1f"));
    m_s_monitor.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                       Dlu(h, kEditRight, 152, kEditRight + 10, 165), this, IDC_OPT_SPIN_MONITOR);
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
