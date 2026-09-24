// OptionsDlg.h : 硬件模型 / 采样 / 校准 / 整机口径设置
#pragma once

#include "resource.h"
#include "Config.h"

class COptionsDlg : public CDialogEx
{
    DECLARE_DYNAMIC(COptionsDlg)
public:
    COptionsDlg(const Config& cfg, CWnd* parent = nullptr);
    virtual ~COptionsDlg();
    enum { IDD = IDD_OPTIONS };

    const Config& config() const { return m_cfg; }

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();

private:
    Config m_cfg;
    CFont m_font;

    CEdit m_e_ppt;
    CEdit m_e_idle;
    CEdit m_e_exp;
    CEdit m_e_baseline;
    CEdit m_e_interval;
    CEdit m_e_calib;
    CEdit m_e_monitor;
    CButton m_chk_monitor;

    CSpinButtonCtrl m_s_ppt;
    CSpinButtonCtrl m_s_idle;
    CSpinButtonCtrl m_s_baseline;
    CSpinButtonCtrl m_s_monitor;

    CStatic m_lbl_monitor;

    // 行标签：必须是**持久成员**，不能用局部 CStatic + Detach()。
    // Detach() 会让字体丢失（WM_GETFONT 变 NULL），控件回退到系统默认字体渲染，
    // 结果文字比按 m_font 算出来的宽 ~23%，右对齐标签从左侧被裁掉。
    // 组框标签同理（见 m_grp_*）。
    CStatic m_lbl_ppt;
    CStatic m_lbl_idle;
    CStatic m_lbl_exp;
    CStatic m_lbl_baseline;
    CStatic m_lbl_interval;
    CStatic m_lbl_calib;

    // 组框必须是 CButton：BS_GROUPBOX 是**按钮类**样式。
    // 早期用 CStatic 创建，MFC 会建出 "STATIC" 类窗口，而 BS_GROUPBOX 的位模式
    // (0x7) 在 STATIC 里等价于 SS_BLACKFRAME —— 结果只画一个空矩形、标题永远不显示。
    CButton m_grp_hw;
    CButton m_grp_sample;
    CButton m_grp_scope;
    void createRow(CStatic& label, CEdit& edit, const CString& text,
                   int x, int y, int label_w, int edit_w, UINT nid);
    CStatic* createPlainLabel(const CString& text, int x, int y, int w, int h);
};
