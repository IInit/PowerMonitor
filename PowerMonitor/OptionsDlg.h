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

    void createRow(CStatic& label, CEdit& edit, const CString& text,
                   int x, int y, int label_w, int edit_w, UINT nid);
    CStatic* createPlainLabel(const CString& text, int x, int y, int w, int h);
};
