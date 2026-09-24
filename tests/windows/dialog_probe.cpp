// tariff_probe.cpp : 插件对话框可观测性探针（电价设置 / 选项设置）
//
// 目的：把"字段到底有没有被创建 / 有没有显示 / 保存有没有生效"从猜测变成事实。
// 做法：LoadLibrary 加载 PowerMonitor.dll，在工作线程上以模态方式打开对话框，
//       主线程抓取该 HWND，枚举全部子控件，检查 类名 / 控件ID / 可见性 / 客户区坐标 / 文本。
//
// 用法：DlgProbe.exe <PowerMonitor.dll> [config_dir]
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>

#include "include/PluginInterface.h"

typedef ITMPlugin* (*PFN_GetInstance)();

static const int CMD_TARIFF = 1;

// ---- 与 resource.h 对应 ----
static const int IDD_TARIFF_ID = 121;
static const int IDD_OPTIONS_ID = 120;
static const int IDC_TAR_REGION_ID = 1100;
static const int IDC_TAR_PLAN_ID = 1101;
static const int IDC_TAR_PEAK_ID = 1102;
static const int IDC_TAR_FLAT_ID = 1103;
static const int IDC_TAR_VALLEY_DRY_ID = 1104;
static const int IDC_TAR_VALLEY_WET_ID = 1105;
static const int IDC_TAR_PEAK_HRS_ID = 1106;
static const int IDC_TAR_VALLEY_HRS_ID = 1107;
static const int IDC_TAR_WET_MONTHS_ID = 1108;
static const int IDC_TAR_META_ID = 1109;
static const int IDC_TAR_APPLY_ID = 1110;

struct CtrlInfo
{
    HWND hwnd = nullptr;
    std::wstring cls;
    int id = 0;
    bool visible = false;
    bool inside = false;
    RECT rc{};
};

static int g_fail = 0;
static void Check(bool ok, const char* what, const char* detail = "")
{
    std::printf("  %s  %s%s%s\n", ok ? "[ OK ]" : "[FAIL]", what,
                ok || *detail == 0 ? "" : " | ", detail);
    if (!ok)
        ++g_fail;
}

static std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::wstring WindowText(HWND h)
{
    if (h == nullptr)
        return std::wstring();
    int n = ::GetWindowTextLengthW(h);
    if (n <= 0)
        return std::wstring();
    std::wstring s((size_t)n, L'\0');
    ::GetWindowTextW(h, &s[0], n + 1);
    return s;
}

static BOOL CALLBACK FindDlgProc(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    ::GetWindowThreadProcessId(h, &pid);
    if (pid != ::GetCurrentProcessId())
        return TRUE;
    wchar_t cls[64] = { 0 };
    ::GetClassNameW(h, cls, 64);
    if (::wcscmp(cls, L"#32770") != 0)
        return TRUE;
    *reinterpret_cast<HWND*>(lp) = h;
    return FALSE;
}

static HWND FindOurDialog()
{
    HWND found = nullptr;
    ::EnumWindows(FindDlgProc, (LPARAM)&found);
    return found;
}

// 列出本进程所有顶层窗口（用于发现挡住流程的消息框）
struct TopWnd { HWND h; std::wstring cls; std::wstring title; };
static std::vector<TopWnd>* g_top = nullptr;

static BOOL CALLBACK EnumTopProc(HWND h, LPARAM)
{
    DWORD pid = 0;
    ::GetWindowThreadProcessId(h, &pid);
    if (pid != ::GetCurrentProcessId() || g_top == nullptr)
        return TRUE;
    TopWnd t;
    t.h = h;
    wchar_t cls[64] = { 0 };
    ::GetClassNameW(h, cls, 64);
    t.cls = cls;
    t.title = WindowText(h);
    g_top->push_back(t);
    return TRUE;
}

static std::vector<TopWnd> TopWindows()
{
    std::vector<TopWnd> v;
    g_top = &v;
    ::EnumWindows(EnumTopProc, 0);
    g_top = nullptr;
    return v;
}

static void DumpTopWindows(const char* tag)
{
    std::printf("  [INFO] %s：本进程顶层窗口：\n", tag);
    for (const TopWnd& t : TopWindows())
        std::printf("         hwnd=%p class=%-16s title=%s\n",
                    t.h, ToUtf8(t.cls).c_str(), ToUtf8(t.title).c_str());
}

// 关掉不属于 keep 的 #32770（即弹出的模态提示框），返回处理个数
static int DismissStrayMessageBoxes(HWND keep)
{
    int n = 0;
    for (const TopWnd& t : TopWindows())
    {
        if (t.cls != L"#32770" || t.h == keep)
            continue;
        ::PostMessageW(t.h, WM_COMMAND, IDOK, 0);
        ::PostMessageW(t.h, WM_CLOSE, 0, 0);
        ++n;
    }
    return n;
}

static std::vector<CtrlInfo> EnumChildren(HWND dlg){
    RECT cr{};
    ::GetClientRect(dlg, &cr);
    std::vector<CtrlInfo> out;
    for (HWND h = ::GetWindow(dlg, GW_CHILD); h != nullptr; h = ::GetWindow(h, GW_HWNDNEXT))
    {
        CtrlInfo c;
        c.hwnd = h;
        wchar_t cls[64] = { 0 };
        ::GetClassNameW(h, cls, 64);
        c.cls = cls;
        c.id = ::GetDlgCtrlID(h);
        c.visible = ::IsWindowVisible(h) != FALSE;
        RECT r{};
        ::GetWindowRect(h, &r);
        POINT pt{ r.left, r.top };
        ::ScreenToClient(dlg, &pt);
        c.rc = { pt.x, pt.y, pt.x + (r.right - r.left), pt.y + (r.bottom - r.top) };
        c.inside = c.rc.left >= 0 && c.rc.top >= 0 &&
                   c.rc.right <= (cr.right - cr.left) && c.rc.bottom <= (cr.bottom - cr.top);
        out.push_back(c);
    }
    return out;
}

static void DumpChildren(HWND dlg, const char* title)
{
    std::vector<CtrlInfo> kids = EnumChildren(dlg);
    RECT cr{};
    ::GetClientRect(dlg, &cr);
    std::printf("\n---- %s：子控件 %d 个（客户区 %ldx%ld）----\n",
                title, (int)kids.size(), (long)(cr.right - cr.left), (long)(cr.bottom - cr.top));
    int vis = 0, inarea = 0;
    for (const CtrlInfo& c : kids)
    {
        std::wstring t = WindowText(c.hwnd);
        if (t.size() > 40)
            t = t.substr(0, 40) + L"…";
        if (c.visible) ++vis;
        if (c.visible && c.inside) ++inarea;
        std::printf("  id=%-5d %-12s vis=%d rect=(%4ld,%4ld,%4ld,%4ld) %s text=%s\n",
                    c.id, ToUtf8(c.cls).c_str(), c.visible ? 1 : 0,
                    (long)c.rc.left, (long)c.rc.top, (long)c.rc.right, (long)c.rc.bottom,
                    c.inside ? "  " : "OUT", ToUtf8(t).c_str());
    }
    std::printf("  可见 %d / 总 %d ；可见且在客户区内 %d\n", vis, (int)kids.size(), inarea);
    Check(vis == (int)kids.size(), "所有子控件都可见");
    Check(inarea == (int)kids.size(), "所有子控件都落在客户区内（没有被裁剪）");
}

// 检查某个 ID 的控件存在、可见且完全落在客户区内
static bool CheckCtrl(HWND dlg, int id, const char* name, bool require_text)
{
    HWND h = ::GetDlgItem(dlg, id);
    if (h == nullptr)
    {
        Check(false, name, "控件不存在");
        return false;
    }
    std::vector<CtrlInfo> all = EnumChildren(dlg);
    bool inside = false;
    for (const CtrlInfo& c : all)
        if (c.hwnd == h)
            inside = c.inside;
    std::wstring t = WindowText(h);
    if (t.size() > 30)
        t = t.substr(0, 30) + L"…";
    bool visible = ::IsWindowVisible(h) != FALSE;
    bool text_ok = !require_text || !t.empty();
    char detail[256];
    std::snprintf(detail, sizeof(detail), "visible=%d inside=%d text=\"%s\"",
                  visible ? 1 : 0, inside ? 1 : 0, ToUtf8(t).c_str());
    Check(visible && inside && text_ok, name, detail);
    return visible && inside;
}

static ITMPlugin::OptionReturn g_opt_ret = ITMPlugin::OR_OPTION_NOT_PROVIDED;
static DWORD g_opt_err = 0;
static bool g_opt_called = false;

static HWND OpenDialog(ITMPlugin* plugin, std::thread& worker, int cmd, int timeout_ms)
{
    worker = std::thread([plugin, cmd]() {
        if (cmd == CMD_TARIFF)
        {
            plugin->OnPluginCommand(CMD_TARIFF, nullptr, nullptr);
        }
        else
        {
            g_opt_called = true;
            g_opt_ret = plugin->ShowOptionsDialog(nullptr);
            g_opt_err = ::GetLastError();
        }
    });

    HWND dlg = nullptr;
    for (int waited = 0; waited < timeout_ms && dlg == nullptr; waited += 50)
    {
        dlg = FindOurDialog();
        if (dlg == nullptr)
            ::Sleep(50);
    }
    if (dlg == nullptr && cmd != CMD_TARIFF)
    {
        std::printf("  [INFO] ShowOptionsDialog 已调用=%d 返回=%d GetLastError=%lu\n",
                    g_opt_called ? 1 : 0, (int)g_opt_ret, g_opt_err);
    }
    return dlg;
}

// 关闭对话框；超过 5s 未关闭则打印诊断并直接退出，避免探针永久挂死
static bool CloseDialog(HWND dlg, std::thread& worker, int cmd, const char* tag)
{
    ::PostMessageW(dlg, WM_COMMAND, (WPARAM)cmd, 0);
    for (int i = 0; i < 100 && ::IsWindow(dlg); ++i)
    {
        DismissStrayMessageBoxes(dlg);
        ::Sleep(50);
    }
    if (::IsWindow(dlg))
    {
        std::printf("  [WARN] %s：对话框 5s 内未关闭（可能被模态提示框挡住）\n", tag);
        DumpTopWindows(tag);
        Check(false, tag, "对话框未能关闭");
        std::printf("\n==== FAIL：探针提前退出 ====\n");
        std::exit(3);
    }
    if (worker.joinable())
        worker.join();
    return true;
}

// 点「应用」按钮。wait=true 走 SendMessage（同步等结果），false 走 PostMessage
// （弹模态框时不会把主线程一起挡住）
static void ClickApply(HWND dlg, bool wait)
{
    HWND b = ::GetDlgItem(dlg, IDC_TAR_APPLY_ID);
    if (b == nullptr)
        return;
    if (wait)
        ::SendMessageW(b, BM_CLICK, 0, 0);
    else
        ::PostMessageW(b, BM_CLICK, 0, 0);
}

static std::string ReadAll(const std::wstring& path)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr)
        return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0)
        s.append(buf, n);
    std::fclose(fp);
    return s;
}

static bool Contains(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

int wmain(int argc, wchar_t** argv)
{
    ::SetConsoleOutputCP(65001);
    setvbuf(stdout, nullptr, _IONBF, 0);   // 不缓冲，卡住时也能看到进度
    if (argc < 2)
    {
        std::printf("usage: DlgProbe.exe <PowerMonitor.dll> [config_dir]\n");
        return 2;
    }
    const wchar_t* dll_path = argv[1];
    std::wstring cfg_dir = argc >= 3 ? argv[2] : L"";
    if (cfg_dir.empty())
    {
        cfg_dir = dll_path;
        size_t pos = cfg_dir.find_last_of(L"\\/");
        cfg_dir = (pos == std::wstring::npos ? std::wstring(L".") : cfg_dir.substr(0, pos)) + L"\\_probecfg";
    }
    std::wstring cfg_dir_deep = cfg_dir + L"\\nested\\dir";   // 故意用不存在的目录测兜底创建
    ::CreateDirectoryW(cfg_dir.c_str(), nullptr);
    std::wstring cfg_file = cfg_dir + L"\\PowerMonitor_config.json";
    ::DeleteFileW(cfg_file.c_str());

    std::printf("DLL: %s\nCFG: %s\n\n", ToUtf8(dll_path).c_str(), ToUtf8(cfg_dir).c_str());

    HMODULE mod = ::LoadLibraryW(dll_path);
    if (mod == nullptr)
    {
        std::printf("[FAIL] LoadLibrary failed, err=%lu\n", ::GetLastError());
        return 1;
    }
    PFN_GetInstance get_instance = (PFN_GetInstance)::GetProcAddress(mod, "TMPluginGetInstance");
    if (get_instance == nullptr)
    {
        std::printf("[FAIL] TMPluginGetInstance not found\n");
        return 1;
    }
    ITMPlugin* plugin = get_instance();
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, cfg_dir.c_str());
    plugin->OnInitialize(nullptr);
    std::printf("[ OK ] plugin initialized\n");

    // ---- 模板资源齐备性（DoModal 找不到模板会直接返回 -1，表现为"窗口根本不出现"） ----
    std::printf("\n---- RT_DIALOG 模板资源检查 ----\n");
    for (int id : { IDD_OPTIONS_ID, IDD_TARIFF_ID, 122 })
    {
        HRSRC r = ::FindResourceW(mod, MAKEINTRESOURCEW(id), RT_DIALOG);
        std::printf("  #%-4d %s\n", id, r != nullptr ? "存在" : "缺失");
        Check(r != nullptr, id == IDD_OPTIONS_ID ? "资源 IDD_OPTIONS 存在"
                                                 : (id == IDD_TARIFF_ID ? "资源 IDD_TARIFF 存在"
                                                                        : "资源 IDD_STATS 存在"));
    }

    // ==================================================== Phase 1 电价对话框渲染
    std::printf("\n============ Phase 1：电价设置 —— 控件渲染 ============\n");
    std::thread worker;
    HWND dlg = OpenDialog(plugin, worker, CMD_TARIFF, 8000);
    Check(dlg != nullptr, "电价对话框已创建");
    if (dlg == nullptr)
    {
        if (worker.joinable())
            worker.detach();
        return 1;
    }
    Check(WindowText(dlg) == L"电价设置", "标题 == 电价设置（确认加载的是本 DLL 的模板）");
    ::Sleep(400);
    DumpChildren(dlg, "电价设置 控件树");

    std::printf("\n---- 字段级检查 ----\n");
    CheckCtrl(dlg, IDOK, "「确定」按钮可见且未被裁剪", false);
    CheckCtrl(dlg, IDCANCEL, "「取消」按钮可见且未被裁剪", false);
    CheckCtrl(dlg, IDC_TAR_APPLY_ID, "「应用」按钮可见且未被裁剪", false);
    CheckCtrl(dlg, IDC_TAR_REGION_ID, "省份下拉：有选中项", true);
    CheckCtrl(dlg, IDC_TAR_PLAN_ID, "方案下拉：有选中项", true);
    CheckCtrl(dlg, IDC_TAR_PEAK_ID, "峰段电价：有值", true);
    CheckCtrl(dlg, IDC_TAR_FLAT_ID, "平段电价：有值", true);
    CheckCtrl(dlg, IDC_TAR_VALLEY_DRY_ID, "谷段电价(枯/平)：有值", true);
    CheckCtrl(dlg, IDC_TAR_VALLEY_WET_ID, "谷段电价(丰水)：有值", true);
    CheckCtrl(dlg, IDC_TAR_VALLEY_HRS_ID, "谷段时段：有值", true);
    CheckCtrl(dlg, IDC_TAR_WET_MONTHS_ID, "丰水月：有值", true);
    CheckCtrl(dlg, IDC_TAR_META_ID, "来源信息字段：已创建且非空", true);

    // ==================================================== Phase 2 「应用」按钮
    std::printf("\n============ Phase 2：「应用」按钮即时保存 ============\n");
    ::SetDlgItemTextW(dlg, IDC_TAR_PEAK_ID, L"0.9876");
    ::SetDlgItemTextW(dlg, IDC_TAR_PEAK_HRS_ID, L"8-22");
    ::SetDlgItemTextW(dlg, IDC_TAR_WET_MONTHS_ID, L"6,7,8");
    ::Sleep(100);
    HWND btn_apply = ::GetDlgItem(dlg, IDC_TAR_APPLY_ID);
    Check(btn_apply != nullptr, "取到「应用」按钮句柄");
    ClickApply(dlg, true);
    ::Sleep(300);

    std::wstring meta_after = WindowText(::GetDlgItem(dlg, IDC_TAR_META_ID));
    std::printf("  状态回显：%s\n", ToUtf8(meta_after.substr(0, 80)).c_str());
    Check(Contains(ToUtf8(meta_after), "已保存"), "「应用」后状态区出现『已保存』回显");
    Check(::IsWindow(dlg), "「应用」后对话框仍处于打开状态");
    std::string json1 = ReadAll(cfg_file);
    Check(Contains(json1, "0.9876"), "「应用」已把新峰段电价写入磁盘");
    Check(Contains(json1, "8-22"), "「应用」已把新峰段时段写入磁盘");

    // ==================================================== Phase 3 确定按钮
    std::printf("\n============ Phase 3：「确定」按钮保存并关闭 ============\n");
    ::SetDlgItemTextW(dlg, IDC_TAR_FLAT_ID, L"0.6666");
    ::Sleep(100);
    CloseDialog(dlg, worker, IDOK, "Phase 3 确定按钮");
    ::Sleep(300);
    std::string json2 = ReadAll(cfg_file);
    Check(Contains(json2, "0.6666"), "「确定」已把平段电价写入磁盘");

    // ==================================================== Phase 4 回读
    std::printf("\n============ Phase 4：重新打开，检查回显 ============\n");
    std::thread worker2;
    HWND dlg2 = OpenDialog(plugin, worker2, CMD_TARIFF, 8000);
    Check(dlg2 != nullptr, "对话框可再次打开");
    if (dlg2 != nullptr)
    {
        ::Sleep(400);
        std::wstring peak = WindowText(::GetDlgItem(dlg2, IDC_TAR_PEAK_ID));
        std::wstring flat = WindowText(::GetDlgItem(dlg2, IDC_TAR_FLAT_ID));
        std::wstring hrs = WindowText(::GetDlgItem(dlg2, IDC_TAR_PEAK_HRS_ID));
        std::wstring wet = WindowText(::GetDlgItem(dlg2, IDC_TAR_WET_MONTHS_ID));
        std::wstring meta = WindowText(::GetDlgItem(dlg2, IDC_TAR_META_ID));
        std::printf("  峰段=%.20s 平段=%.20s 峰段时段=%.20s 丰水月=%.20s\n",
                    ToUtf8(peak).c_str(), ToUtf8(flat).c_str(),
                    ToUtf8(hrs).c_str(), ToUtf8(wet).c_str());
        Check(peak == L"0.9876", "峰段电价回显 == 0.9876");
        Check(flat == L"0.6666", "平段电价回显 == 0.6666");
        Check(hrs == L"8-22", "峰段时段回显 == 8-22");
        Check(wet == L"6,7,8", "丰水月回显 == 6,7,8");
        Check(!meta.empty(), "来源信息非空");
        CloseDialog(dlg2, worker2, IDCANCEL, "Phase 4 取消按钮");
    }

    // ==================================================== Phase 5 选项对话框
    std::printf("\n============ Phase 5：选项设置对话框布局 ============\n");
    std::thread worker3;
    HWND dlg3 = OpenDialog(plugin, worker3, IDD_OPTIONS_ID, 8000);
    Check(dlg3 != nullptr, "选项设置对话框已创建");
    if (dlg3 != nullptr)
    {
        ::Sleep(400);
        DumpChildren(dlg3, "选项设置 控件树");
        CheckCtrl(dlg3, IDOK, "「确定」按钮可见且未被裁剪", false);
        CheckCtrl(dlg3, IDCANCEL, "「取消」按钮可见且未被裁剪", false);
        CloseDialog(dlg3, worker3, IDCANCEL, "Phase 5 取消按钮");
    }

    // ==================================================== Phase 6 配置目录兜底
    std::printf("\n============ Phase 6：多级目录不存在时的兜底 ============\n");
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, cfg_dir_deep.c_str());
    ::RemoveDirectoryW((cfg_dir + L"\\nested\\dir").c_str());
    ::RemoveDirectoryW((cfg_dir + L"\\nested").c_str());
    {
        std::thread w4;
        HWND d = OpenDialog(plugin, w4, CMD_TARIFF, 8000);
        Check(d != nullptr, "对话框已打开");
        if (d != nullptr)
        {
            ::Sleep(300);
            ::SetDlgItemTextW(d, IDC_TAR_PEAK_ID, L"0.1234");
            ClickApply(d, true);
            ::Sleep(300);
            std::wstring meta = WindowText(::GetDlgItem(d, IDC_TAR_META_ID));
            std::printf("  状态回显：%s\n", ToUtf8(meta.substr(0, 90)).c_str());
            Check(Contains(ToUtf8(meta), "已保存"), "目录不存在时也能保存（自动补全上级目录）");
            std::string deep = ReadAll(cfg_dir_deep + L"\\PowerMonitor_config.json");
            Check(Contains(deep, "0.1234"), "深层目录配置已写出");
            CloseDialog(d, w4, IDCANCEL, "Phase 6 取消按钮");
        }
    }

    // ==================================================== Phase 7 不可写路径
    std::printf("\n============ Phase 7：不可写路径必须明确报错，不能静默 ============\n");
    std::wstring bad_dir = std::wstring(dll_path) + L"\\sub";   // 在"文件"下面建目录，必然失败
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, bad_dir.c_str());
    {
        std::thread w5;
        HWND d = OpenDialog(plugin, w5, CMD_TARIFF, 8000);
        Check(d != nullptr, "对话框已打开");
        if (d != nullptr)
        {
            ::Sleep(300);
            ::SetDlgItemTextW(d, IDC_TAR_PEAK_ID, L"0.4321");
            ClickApply(d, false);   // PostMessage：模态提示框不会阻塞本线程
            int dismissed = 0;
            for (int i = 0; i < 60 && ::IsWindow(d); ++i)
            {
                dismissed += DismissStrayMessageBoxes(d);
                if (dismissed > 0)
                    break;
                ::Sleep(50);
            }
            ::Sleep(400);
            DismissStrayMessageBoxes(d);
            std::wstring meta = WindowText(::GetDlgItem(d, IDC_TAR_META_ID));
            std::printf("  状态回显：%s\n", ToUtf8(meta.substr(0, 90)).c_str());
            Check(dismissed > 0, "写盘失败时弹出了可见的错误提示框");
            Check(Contains(ToUtf8(meta), "保存失败"), "状态区明确提示『保存失败』");
            Check(::IsWindow(d) != FALSE, "失败后对话框保持打开，没有被静默关闭");
            CloseDialog(d, w5, IDCANCEL, "Phase 7 取消按钮");
        }
    }

    ::FreeLibrary(mod);
    std::printf("\n==== %s (%d failure) ====\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
