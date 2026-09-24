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

    ApplyHandler m_on_apply;

    void fillPlans(const Region* r);
    void fillEdits(const Region* r, const Plan* p);
    void fillMeta(const Region* r, const Plan* p);
    void refreshMeta();    // 把 m_status + m_meta_info 写进 m_meta
    bool collect();        // 把控件当前值收进 m_cfg，控件缺失时返回 false
};
