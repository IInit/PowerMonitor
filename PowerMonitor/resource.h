//{{NO_DEPENDENCIES}}
// resource.h : 资源与控件 ID
// 注意：注释行必须以 ASCII 字符结尾。
// 本文件会被 rc.exe 按 ANSI(GBK) 解码，若某行末尾是非 ASCII 字节（如中文），
// 该字节会与换行符组成一个双字节字符，把下一行的 #define 吞进注释，
// 导致该 ID 未定义、资源被存成字符串名而运行时找不到。

#define IDI_POWERMONITOR                101

// ---- 对话框 ----
#define IDD_OPTIONS                     120
#define IDD_TARIFF                      121
#define IDD_STATS                       122

// ---- OptionsDlg 控件 ----
#define IDC_OPT_CPU_PPT                 1000
#define IDC_OPT_CPU_IDLE                1001
#define IDC_OPT_CPU_EXP                 1002
#define IDC_OPT_BASELINE                1003
#define IDC_OPT_INTERVAL                1004
#define IDC_OPT_CALIBRATION             1005
#define IDC_OPT_INC_MONITOR             1006
#define IDC_OPT_MONITOR_W               1007
#define IDC_OPT_SPIN_PPT                1010
#define IDC_OPT_SPIN_IDLE               1011
#define IDC_OPT_SPIN_BASELINE           1012
#define IDC_OPT_SPIN_MONITOR            1013

// ---- TariffDlg 控件 ----
#define IDC_TAR_REGION                  1100
#define IDC_TAR_PLAN                    1101
#define IDC_TAR_PEAK                    1102
#define IDC_TAR_FLAT                    1103
#define IDC_TAR_VALLEY_DRY              1104
#define IDC_TAR_VALLEY_WET              1105
#define IDC_TAR_PEAK_HRS                1106
#define IDC_TAR_VALLEY_HRS              1107
#define IDC_TAR_WET_MONTHS              1108
#define IDC_TAR_META                    1109
#define IDC_TAR_APPLY                   1110

// ---- StatsDlg 控件 ----
#define IDC_STATS_TAB                   1200
#define IDC_STATS_LIST                  1201
#define IDC_STATS_SUMMARY               1202

// ---- 版本信息 ----
#define VS_VERSION_INFO                 1

// Next default values for new objects
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE        130
#define _APS_NEXT_COMMAND_VALUE         32771
#define _APS_NEXT_CONTROL_VALUE         1300
#define _APS_NEXT_SYMED_VALUE           101
#endif
#endif
