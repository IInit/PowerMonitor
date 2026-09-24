// tariff_probe.cpp : 插件对话框可观测性探针（电价设置 / 选项设置）
//
// 目的：把"字段到底有没有被创建 / 有没有显示 / 保存有没有生效"从猜测变成事实。
// 做法：LoadLibrary 加载 PowerMonitor.dll，在工作线程上以模态方式打开对话框，
//       主线程抓取该 HWND，枚举全部子控件，检查 类名 / 控件ID / 可见性 / 客户区坐标 / 文本。
//
// 用法：DlgProbe.exe <PowerMonitor.dll> [config_dir] [png_out_dir]
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>

#include "include/PluginInterface.h"

typedef ITMPlugin* (*PFN_GetInstance)();

// 截图输出目录（命令行第 3 个参数，空则不截图）
static std::wstring g_png_dir;

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

// ------------------------------------------------------------------ 文字是否放得下
// 用控件自身的字体测量文字的实际渲染尺寸，与控件客户区比较。
// 这是"控件存在、可见、位置正确，但屏幕上看着被截断"这类问题的判定依据——
// 前面的检查只看矩形，看不到文字溢出。
struct TextFit
{
    int need_w = 0, need_h = 0;   // 文字需要的像素
    int have_w = 0, have_h = 0;   // 控件客户区像素
    int font_h = 0;               // 控件字体高度（诊断用）
    int dpi = 0;                  // 控件 DC 的垂直 DPI（诊断用）
    bool multiline = false;       // 该控件按多行排版测量
    bool ok = true;
};

// 该控件是否允许文字自动折行？
//   多行 Static（SS_LEFT / 默认）会折行；SS_SIMPLE、SS_RIGHT、按钮、编辑框都不折。
// 判定依据是窗口样式，而不是"文字里有没有 \n"——判据要跟控件真实渲染一致。
static bool AllowsWrapping(HWND h)
{
    wchar_t cls[64] = { 0 };
    ::GetClassNameW(h, cls, 64);
    if (::wcscmp(cls, L"Static") != 0)
        return false;

    LONG style = ::GetWindowLongW(h, GWL_STYLE);
    if (style & SS_SIMPLE)
        return false;                       // SS_SIMPLE 明确不折行
    LONG align = style & SS_TYPEMASK;       // 取对齐类型
    if (align == SS_RIGHT || align == SS_CENTER)
        return false;                       // 右/居中的单行标签
    return true;                            // SS_LEFT / SS_LEFTNOWORDWRAP 之外默认按多行
}

static TextFit MeasureTextFit(HWND h)
{
    TextFit f;
    RECT rc{};
    ::GetClientRect(h, &rc);
    f.have_w = rc.right - rc.left;
    f.have_h = rc.bottom - rc.top;

    std::wstring t = WindowText(h);
    if (t.empty())
        return f;

    HDC dc = ::GetDC(h);
    if (dc == nullptr)
        return f;

    // 用控件当前字体测量，才反映真实渲染结果
    HFONT font = (HFONT)::SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ old = nullptr;
    if (font != nullptr)
        old = ::SelectObject(dc, font);

    LOGFONTW lf{};
    if (font)
        ::GetObjectW(font, sizeof(lf), &lf);
    f.font_h = lf.lfHeight;
    f.dpi = ::GetDeviceCaps(dc, LOGPIXELSY);

    f.multiline = AllowsWrapping(h) && f.have_w > 0;
    if (f.multiline)
    {
        // 多行控件：按**控件真实宽度**排版，看需要的行高是否放得下。
        // 这正是控件自己的渲染方式（含显式 \n 与自动折行），
        // 用 DT_SINGLELINE 量它会得出"超级宽的一行"这种假阳性。
        RECT calc{ 0, 0, f.have_w, 0 };
        ::DrawTextW(dc, t.c_str(), (int)t.size(), &calc,
                    DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        f.need_w = calc.right - calc.left;    // 折行后最宽一行的宽度
        f.need_h = calc.bottom - calc.top;    // 折行后需要的总高度
    }
    else
    {
        RECT calc{ 0, 0, 0, 0 };
        ::DrawTextW(dc, t.c_str(), (int)t.size(), &calc,
                    DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        f.need_w = calc.right - calc.left;
        f.need_h = calc.bottom - calc.top;
    }

    if (old != nullptr)
        ::SelectObject(dc, old);
    ::ReleaseDC(h, dc);

    // 单行控件给一点余量：不同字体渲染有 1-2px 抖动。
    // 多行控件只需保证"折行后高度放得下"——宽度由排版保证 <= have_w。
    if (f.multiline)
        f.ok = (f.need_h <= f.have_h);
    else
        f.ok = (f.need_w <= f.have_w) && (f.need_h <= f.have_h);
    return f;
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

// ------------------------------------------------------------------ 截图
// 把对话框窗口渲染成 PNG，便于人工核对"文字是否完整显示"——
// 自动化断言只能证明几何放得下，截图才是给人看的最终证据。
static bool SaveDialogPng(HWND dlg, const wchar_t* path)
{
    RECT rc{};
    if (!::GetWindowRect(dlg, &rc))
        return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return false;

    // 先强制重绘并给消息循环一点时间，否则 PrintWindow(PW_RENDERFULLCONTENT)
    // 可能抓到"还没来得及画"的空白状态（截图与渲染竞争）。
    ::RedrawWindow(dlg, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
    ::UpdateWindow(dlg);
    ::Sleep(500);

    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);

    // 用 WM_PRINT 让**所有子控件**都把自己画进我们的 DC。
    // 比 PrintWindow 更可靠：PrintWindow 在某些合成/主题场景下只画标题栏。
    ::SendMessageW(dlg, WM_PRINT, (WPARAM)mem,
                   PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);

    // 用 GDI+ 编码 PNG（探针已链接 gdiplus.lib）
    Gdiplus::Bitmap out(bmp, nullptr);
    CLSID clsid;
    bool ok = false;
    if (::CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &clsid) == S_OK)
        ok = (out.Save(path, &clsid, nullptr) == Gdiplus::Ok);

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return ok;
}

static void DumpChildren(HWND dlg, const char* title)
{
    std::vector<CtrlInfo> kids = EnumChildren(dlg);
    RECT cr{};
    ::GetClientRect(dlg, &cr);
    std::printf("\n---- %s：子控件 %d 个（客户区 %ldx%ld）----\n",
                title, (int)kids.size(), (long)(cr.right - cr.left), (long)(cr.bottom - cr.top));
    int vis = 0, inarea = 0, overflow = 0;
    for (const CtrlInfo& c : kids)
    {
        std::wstring t = WindowText(c.hwnd);
        std::wstring shown = t;
        if (shown.size() > 40)
            shown = shown.substr(0, 40) + L"…";
        for (wchar_t& ch : shown)
            if (ch == L'\n' || ch == L'\r')
                ch = L'|';   // 换行转成可见符号，否则打印会在那里截断
        if (c.visible) ++vis;
        if (c.visible && c.inside) ++inarea;

        // 文字放不下时明确标出，并给出需要/可用的像素宽度
        TextFit fit = MeasureTextFit(c.hwnd);
        char fitNote[128] = "";
        const char* fitFlag = "   ";
        if (!fit.ok && !t.empty())
        {
            ++overflow;
            fitFlag = "TXT";
            std::snprintf(fitNote, sizeof(fitNote),
                          "  [文字溢出 need=%dx%d have=%dx%d%s fontH=%d dpi=%d]",
                          fit.need_w, fit.need_h, fit.have_w, fit.have_h,
                          fit.multiline ? " ML" : "", fit.font_h, fit.dpi);
        }

        std::printf("  id=%-5d %-12s vis=%d rect=(%4ld,%4ld,%4ld,%4ld) %s %s fontH=%-3d text=%s%s\n",
                    c.id, ToUtf8(c.cls).c_str(), c.visible ? 1 : 0,
                    (long)c.rc.left, (long)c.rc.top, (long)c.rc.right, (long)c.rc.bottom,
                    c.inside ? "  " : "OUT", fitFlag, fit.font_h, ToUtf8(shown).c_str(), fitNote);
    }
    std::printf("  可见 %d / 总 %d ；可见且在客户区内 %d ；文字放不下 %d\n",
                vis, (int)kids.size(), inarea, overflow);
    Check(vis == (int)kids.size(), "所有子控件都可见");
    Check(inarea == (int)kids.size(), "所有子控件都落在客户区内（没有被裁剪）");
    Check(overflow == 0, "所有控件的文字都放得下（未被截断）");
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
    g_png_dir = argc >= 4 ? argv[3] : L"";
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

    // GDI+ 用于截图输出 PNG（可选功能，初始化失败不影响断言）
    Gdiplus::GdiplusStartupInput gdi_in;
    ULONG_PTR gdi_token = 0;
    if (!g_png_dir.empty())
        Gdiplus::GdiplusStartup(&gdi_token, &gdi_in, nullptr);

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
    if (!g_png_dir.empty())
    {
        std::wstring p = g_png_dir + L"\\dialog_tariff.png";
        std::printf("  截图：%s %s\n", ToUtf8(p).c_str(),
                    SaveDialogPng(dlg, p.c_str()) ? "已保存" : "失败");
    }

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
        if (!g_png_dir.empty())
        {
            std::wstring p = g_png_dir + L"\\dialog_options.png";
            std::printf("  截图：%s %s\n", ToUtf8(p).c_str(),
                        SaveDialogPng(dlg3, p.c_str()) ? "已保存" : "失败");
        }
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
    if (gdi_token != 0)
        Gdiplus::GdiplusShutdown(gdi_token);
    std::printf("\n==== %s (%d failure) ====\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
