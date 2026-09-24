// StatsDlg.h : 用量统计（按天/月/年/每次开机/时段 五个维度）
#pragma once

#include "resource.h"
#include "Meter.h"

class CStatsDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CStatsDlg)
public:
    CStatsDlg(EnergyMeter& meter, CWnd* parent = nullptr);
    virtual ~CStatsDlg();
    enum { IDD = IDD_STATS };

protected:
    virtual BOOL OnInitDialog();
    afx_msg void OnTabChanged(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    EnergyMeter& m_meter;
    CFont m_font;
    CTabCtrl m_tab;
    CListCtrl m_list;
    CStatic m_summary;
    int m_month = 1;

    void refreshList();
};
