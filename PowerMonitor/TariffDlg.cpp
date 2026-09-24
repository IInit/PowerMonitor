// TariffDlg.cpp : 电价设置实现
//
// 关键约定（修 bug 时勿破坏）：
//  1) 布局全部用对话框单位(DLU)描述，像素坐标一律经 Dlu() 换算。
//     早期版本把客户区强行设成固定 340x420 像素，而模板是 225x260 DLU
//     （≈394x455 像素 @100%DPI），导致模板里的"确定/取消"被裁到客户区之外，
//     用户根本看不到、点不到"确定"——表现为"改完不知道有没有保存"。
//  2) 所有控件必须先全部创建完成，再调用 fill* 回填数据。
//     早期版本先调 fillMeta() 后创建 m_meta，导致"来源信息"字段永远为空。
#include "pch.h"
#include "TariffDlg.h"
#include "Tariffs.h"
#include "Encoding.h"
#include <cstdio>
#include <string>

IMPLEMENT_DYNAMIC(CTariffDlg, CDialogEx)

BEGIN_MESSAGE_MAP(CTariffDlg, CDialogEx)
    ON_CBN_SELCHANGE(IDC_TAR_REGION, &CTariffDlg::OnRegionChanged)
    ON_CBN_SELCHANGE(IDC_TAR_PLAN, &CTariffDlg::OnPlanChanged)
    ON_BN_CLICKED(IDC_TAR_APPLY, &CTariffDlg::OnApply)
END_MESSAGE_MAP()

// 对话框单位 -> 像素（随系统 DPI 与对话框字体自动换算）
static CRect Dlu(HWND hDlg, int l, int t, int r, int b)
{
    CRect rc(l, t, r, b);
    ::MapDialogRect(hDlg, &rc);
    return rc;
}

CTariffDlg::CTariffDlg(const Config& cfg, CWnd* parent)
    : CDialogEx(IDD, parent), m_cfg(cfg)
{
}

CTariffDlg::~CTariffDlg()
{
}

// ---------------------------------------------------------------- 读写辅助
static void setNum(CEdit& e, double v)
{
    if (!e.GetSafeHwnd())
        return;
    CString s;
    s.Format(L"%.4f", v);
    e.SetWindowText(s);
}

static void setText(CEdit& e, const CString& v)
{
    if (e.GetSafeHwnd())
        e.SetWindowText(v);
}

static double readNum(CEdit& e, double fallback)
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
        return fallback;   // 整串都不是数字 -> 保留原值
    return v;
}

static CString readText(CEdit& e)
{
    CString s;
    if (e.GetSafeHwnd())
        e.GetWindowText(s);
    return s;
}

static std::vector<int> parseMonthList(const std::string& s)
{
    std::vector<int> out;
    size_t start = 0;
    while (start <= s.size())
    {
        size_t comma = s.find(',', start);
        std::string chunk = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        try
        {
            int v = std::stoi(chunk);
            if (v >= 1 && v <= 12)
                out.push_back(v);
        }
        catch (...) {}
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

static std::string joinMonths(const std::vector<int>& v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i)
            s += ",";
        char b[8];
        std::snprintf(b, sizeof(b), "%d", v[i]);
        s += b;
    }
    return s;
}

// ---------------------------------------------------------------- 初始化
BOOL CTariffDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    HWND h = GetSafeHwnd();

    // 客户区按模板的对话框单位展开：模板按钮位于 y=242..258 DLU，
    // 只要客户区高度 >= 模板高度，它们就必然可见、可点。
    CRect full = Dlu(h, 0, 0, DLG_W, DLG_H);
    RECT wr = { 0, 0, full.right - full.left, full.bottom - full.top };
    AdjustWindowRectEx(&wr, GetStyle(), FALSE, GetExStyle());
    SetWindowPos(nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();

    m_font.CreatePointFont(90, L"MS Shell Dlg");

    // ---- 省份 / 方案 ----
    CStatic l_region;
    l_region.Create(L"省份/地区", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                    Dlu(h, 8, 8, 50, 20), this);
    l_region.SetFont(&m_font);
    l_region.Detach();
    m_cb_region.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                       Dlu(h, 54, 6, 218, 110), this, IDC_TAR_REGION);
    m_cb_region.SetFont(&m_font);

    CStatic l_plan;
    l_plan.Create(L"用电方案", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                  Dlu(h, 8, 24, 50, 36), this);
    l_plan.SetFont(&m_font);
    l_plan.Detach();
    m_cb_plan.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                     Dlu(h, 54, 22, 218, 110), this, IDC_TAR_PLAN);
    m_cb_plan.SetFont(&m_font);

    // ---- 电价分组 ----
    CStatic grp_price;
    grp_price.Create(L"电价（元/度）", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                     Dlu(h, 5, 40, 219, 104), this);
    grp_price.SetFont(&m_font);
    grp_price.Detach();

    auto priceRow = [&](CEdit& e, const wchar_t* label, double val, int y, UINT nid) {
        CStatic l;
        l.Create(label, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                 Dlu(h, 12, y, 90, y + 12), this);
        l.SetFont(&m_font);
        l.Detach();
        e.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                 Dlu(h, 94, y, 164, y + 12), this, nid);
        e.SetFont(&m_font);
        setNum(e, val);
    };
    priceRow(m_e_peak, L"峰段电价", m_cfg.price_peak, 48, IDC_TAR_PEAK);
    priceRow(m_e_flat, L"平段电价", m_cfg.price_flat, 62, IDC_TAR_FLAT);
    priceRow(m_e_vdry, L"谷段电价（枯/平水期）", m_cfg.price_valley_dry, 76, IDC_TAR_VALLEY_DRY);
    priceRow(m_e_vwet, L"谷段电价（丰水期）", m_cfg.price_valley_wet, 90, IDC_TAR_VALLEY_WET);

    // ---- 时段与丰枯 ----
    CStatic grp_hrs;
    grp_hrs.Create(L"时段与丰枯", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                   Dlu(h, 5, 108, 219, 168), this);
    grp_hrs.SetFont(&m_font);
    grp_hrs.Detach();

    auto hrsRow = [&](CEdit& e, const wchar_t* label, const CString& val, int y, UINT nid) {
        CStatic l;
        l.Create(label, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                 Dlu(h, 12, y, 90, y + 12), this);
        l.SetFont(&m_font);
        l.Detach();
        e.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                 Dlu(h, 94, y, 214, y + 12), this, nid);
        e.SetFont(&m_font);
        e.SetWindowText(val);
    };
    hrsRow(m_e_peakhrs, L"峰段时段（如 8-22）", Utf8ToCString(m_cfg.peak_hours), 116, IDC_TAR_PEAK_HRS);
    hrsRow(m_e_valleyhrs, L"谷段时段（如 23-7）", Utf8ToCString(m_cfg.valley_hours), 130, IDC_TAR_VALLEY_HRS);
    hrsRow(m_e_wetmonths, L"丰水月（逗号分隔）", CString(joinMonths(m_cfg.valley_wet_months).c_str()), 144, IDC_TAR_WET_MONTHS);

    // ---- 状态 / 来源信息（必须在 fill* 之前建好） ----
    m_meta.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                  Dlu(h, 8, 174, 217, 236), this, IDC_TAR_META);
    m_meta.SetFont(&m_font);

    // ---- 控件齐备后再回填 ----
    // 单价 / 时段保持配置中的现值，避免"打开对话框即覆盖用户手改的值"
    fillPlans(nullptr);   // 先清空方案下拉
    const std::vector<Region>& regions = allRegions();
    int select_region = 0;
    int idx = 0;
    for (const std::string& n : regionNames())
    {
        m_cb_region.AddString(Utf8ToCString(n));
        if (n == m_cfg.tariff_region)
            select_region = idx;
        ++idx;
    }
    if (m_cb_region.GetCount() == 0)
        select_region = -1;
    m_cb_region.SetCurSel(select_region);

    const Region* cur_region = nullptr;
    const Plan* cur_plan = nullptr;
    int plan_sel = 0;
    if (select_region >= 0 && select_region < (int)regions.size())
    {
        cur_region = &regions[select_region];
        for (size_t i = 0; i < cur_region->plans.size(); ++i)
        {
            if (cur_region->plans[i].label == m_cfg.tariff_plan)
                plan_sel = (int)i;
        }
        fillPlans(cur_region);
        if (!cur_region->plans.empty())
        {
            m_cb_plan.SetCurSel(plan_sel);
            cur_plan = &cur_region->plans[plan_sel];
        }
    }
    fillMeta(cur_region, cur_plan);
    refreshMeta();

    return TRUE;
}

void CTariffDlg::refreshMeta()
{
    if (!m_meta.GetSafeHwnd())
        return;
    CString text;
    if (!m_status.IsEmpty())
        text = m_status + L"\r\n";
    text += m_meta_info;
    m_meta.SetWindowText(text);
}

// ---------------------------------------------------------------- 数据回填
void CTariffDlg::fillPlans(const Region* r)
{
    if (m_cb_plan.GetSafeHwnd())
        m_cb_plan.ResetContent();
    if (!r || !m_cb_plan.GetSafeHwnd())
        return;
    for (const Plan& p : r->plans)
        m_cb_plan.AddString(Utf8ToCString(p.label));
}

void CTariffDlg::fillEdits(const Region* r, const Plan* p)
{
    if (p)
    {
        setNum(m_e_peak, p->peak);
        setNum(m_e_flat, p->flat);
        setNum(m_e_vdry, p->valley);
        setNum(m_e_vwet, p->resolvedValleyWet());
        setText(m_e_peakhrs, Utf8ToCString(p->peak_hours));
        setText(m_e_valleyhrs, Utf8ToCString(p->valley_hours));
        setText(m_e_wetmonths, CString(joinMonths(p->wet_months).c_str()));
    }

    fillMeta(r, p);
    refreshMeta();
}

void CTariffDlg::fillMeta(const Region* r, const Plan* p)
{
    CString meta;
    if (r)
    {
        CString src = Utf8ToCString(r->source);
        CString eff = Utf8ToCString(r->effective);
        CString note = Utf8ToCString(p ? p->note : "");
        meta.Format(L"来源：%s\r\n生效：%s%s\r\n备注：%s",
                    src.GetString(),
                    eff.GetString(),
                    r->verify ? L"\r\n（网络汇总口径，建议以当地供电公司/电费账单为准）" : L"",
                    note.GetString());
    }
    else
    {
        meta = L"未匹配到地区预设，可直接手工填写单价与时段。";
    }
    m_meta_info = meta;
}

// ---------------------------------------------------------------- 交互
void CTariffDlg::OnRegionChanged()
{
    int sel = m_cb_region.GetCurSel();
    const std::vector<Region>& regions = allRegions();
    if (sel < 0 || sel >= (int)regions.size())
        return;

    const Region& r = regions[sel];
    fillPlans(&r);
    if (r.plans.empty())
        return;   // 无预设方案时保留用户手填值
    m_cb_plan.SetCurSel(0);
    fillEdits(&r, &r.plans[0]);
}

void CTariffDlg::OnPlanChanged()
{
    int rs = m_cb_region.GetCurSel();
    int ps = m_cb_plan.GetCurSel();
    const std::vector<Region>& regions = allRegions();
    if (rs < 0 || rs >= (int)regions.size())
        return;
    const Region& r = regions[rs];
    if (ps < 0 || ps >= (int)r.plans.size())
        return;
    fillEdits(&r, &r.plans[ps]);
}

// 控件值 -> m_cfg
bool CTariffDlg::collect()
{
    // 控件缺失说明对话框初始化失败，此时不要拿空控件去覆盖配置
    if (!m_cb_region.GetSafeHwnd() || !m_e_peak.GetSafeHwnd() ||
        !m_e_flat.GetSafeHwnd() || !m_e_vdry.GetSafeHwnd() || !m_e_vwet.GetSafeHwnd() ||
        !m_e_peakhrs.GetSafeHwnd() || !m_e_valleyhrs.GetSafeHwnd() || !m_e_wetmonths.GetSafeHwnd())
        return false;

    const std::vector<Region>& regions = allRegions();
    int rs = m_cb_region.GetCurSel();
    if (rs >= 0 && rs < (int)regions.size())
    {
        const Region& r = regions[rs];
        m_cfg.tariff_region = r.name;
        m_cfg.tariff_source = r.source;
        m_cfg.tariff_effective = r.effective;
        m_cfg.tariff_verify = r.verify;

        int ps = m_cb_plan.GetCurSel();
        if (ps >= 0 && ps < (int)r.plans.size())
        {
            m_cfg.tariff_plan = r.plans[ps].label;
            m_cfg.tariff_note = r.plans[ps].note;
        }
    }

    m_cfg.price_peak = readNum(m_e_peak, m_cfg.price_peak);
    m_cfg.price_flat = readNum(m_e_flat, m_cfg.price_flat);
    m_cfg.price_valley_dry = readNum(m_e_vdry, m_cfg.price_valley_dry);
    m_cfg.price_valley_wet = readNum(m_e_vwet, m_cfg.price_valley_wet);

    m_cfg.peak_hours = CStringToUtf8(readText(m_e_peakhrs));
    m_cfg.valley_hours = CStringToUtf8(readText(m_e_valleyhrs));
    m_cfg.valley_wet_months = parseMonthList(CStringToUtf8(readText(m_e_wetmonths)));

    // 非负约束
    if (m_cfg.price_peak < 0) m_cfg.price_peak = 0;
    if (m_cfg.price_flat < 0) m_cfg.price_flat = 0;
    if (m_cfg.price_valley_dry < 0) m_cfg.price_valley_dry = 0;
    if (m_cfg.price_valley_wet < 0) m_cfg.price_valley_wet = 0;

    return true;
}

void CTariffDlg::OnApply()
{
    if (!collect())
    {
        m_status = L"● 保存失败：对话框控件未就绪";
        refreshMeta();
        return;
    }

    CString msg;
    bool ok = m_on_apply ? m_on_apply(m_cfg, msg) : false;

    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    if (ok)
    {
        if (msg.IsEmpty())
            m_status.Format(L"● 已保存 %02d:%02d:%02d，新电价已即时生效",
                            st.wHour, st.wMinute, st.wSecond);
        else
            m_status.Format(L"● 已保存 %02d:%02d:%02d → %s",
                            st.wHour, st.wMinute, st.wSecond, msg.GetString());
    }
    else
    {
        CString reason = msg.IsEmpty() ? CString(L"未知原因") : msg;
        m_status.Format(L"● 保存失败 %02d:%02d:%02d：%s",
                        st.wHour, st.wMinute, st.wSecond, reason.GetString());
        ::MessageBoxW(GetSafeHwnd(), reason, L"电价设置", MB_OK | MB_ICONERROR);
    }
    refreshMeta();
}

void CTariffDlg::OnOK()
{
    if (!collect())
    {
        // 控件异常时宁可不开空回去，也不要静默丢失用户输入
        ::MessageBoxW(GetSafeHwnd(), L"对话框控件未就绪，未保存任何修改。",
                      L"电价设置", MB_OK | MB_ICONERROR);
        return;
    }
    CDialogEx::OnOK();
}
