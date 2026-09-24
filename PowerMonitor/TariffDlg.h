// TariffDlg.h : 电价设置（省份/方案预设 + 峰平谷单价 + 时段 + 丰水月）
//
// 布局约定：所有控件坐标都用“对话框单位(DLU)”描述，再由 MapDialogRect 换算成像素，
// 客户区尺寸同样由模板的 DLU 尺寸换算而来。这样在任意 DPI / 字体度量下，
// 模板按钮（应用/确定/取消）与动态控件都不会被裁到客户区外。
#pragma once

#include "resource.h"
#include "Config.h"
#include "Tariffs.h"   // Region / Plan
#include <functional>

class CTariffDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CTariffDlg)
public:
    CTariffDlg(const Config& cfg, CWnd* parent = nullptr);
    virtual ~CTariffDlg();
    enum { IDD = IDD_TARIFF };

    // 模板尺寸（对话框单位）
    enum { DLG_W = 225, DLG_H = 260 };

    const Config& config() const { return m_cfg; }

    // 「应用」回调：由宿主负责把配置落盘。
    //   返回 true  时 msg 用于显示"保存到哪里"；
    //   返回 false 时 msg 用于显示失败原因。
    typedef std::function<bool(const Config&, CString& msg)> ApplyHandler;
    void setApplyHandler(const ApplyHandler& h) { m_on_apply = h; }

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    afx_msg void OnRegionChanged();
    afx_msg void OnPlanChanged();
    afx_msg void OnApply();
    DECLARE_MESSAGE_MAP()

private:
    Config m_cfg;
    CFont m_font;

    CComboBox m_cb_region;
    CComboBox m_cb_plan;

    CEdit m_e_peak;
    CEdit m_e_flat;
    CEdit m_e_vdry;
    CEdit m_e_vwet;
    CEdit m_e_peakhrs;
    CEdit m_e_valleyhrs;
    CEdit m_e_wetmonths;

    CStatic m_meta;        // 状态行 + 来源信息
    CString m_meta_info;   // 来源 / 生效 / 备注
    CString m_status;      // 保存结果状态行

    // 标签 / 组框：必须是持久成员，不能用局部 CStatic + Detach()。
    // Detach() 会让控件字体丢失（WM_GETFONT 变 NULL），改用系统默认字体渲染，
    // 文字会比按 m_font 测量的宽约 23%，右对齐标签从左侧溢出被裁掉。
    CStatic m_lbl_region;
    CStatic m_lbl_plan;
    // 组框必须是 CButton（BS_GROUPBOX 是按钮类样式；用 CStatic 会被当成 SS_BLACKFRAME，
    // 只画空矩形、标题不显示）——详见 OptionsDlg.h 的同类注释
    CButton m_grp_price;
    CButton m_grp_hrs;
    // 两组行标签（各 4 / 3 个，统一列宽对齐）
    CStatic m_lbl_price[4];
    CStatic m_lbl_hrs[3];

    ApplyHandler m_on_apply;

    void fillPlans(const Region* r);
    void fillEdits(const Region* r, const Plan* p);
    void fillMeta(const Region* r, const Plan* p);
    void refreshMeta();    // 把 m_status + m_meta_info 写进 m_meta
    bool collect();        // 把控件当前值收进 m_cfg，控件缺失时返回 false
};
