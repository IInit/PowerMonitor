// smoke_host.cpp : TrafficMonitor 插件宿主冒烟测试
//
// 不依赖 MFC / TrafficMonitor 主程序，直接按 PluginInterface.h 的契约加载插件 DLL：
//   LoadLibrary -> TMPluginGetInstance -> OnExtenedInfo -> OnInitialize
//   -> GetInfo / GetItem 枚举 / DataRequired / GetTooltipInfo / 命令列表
//
// 编译（x64）：见 build_x64.sh 末尾，或
//   cl /nologo /EHsc /std:c++17 /MD /utf-8 smoke_host.cpp /I..\..\PowerMonitor /I..\..\PowerMonitor\include
//
// 用法：PluginSmokeTest.exe <PowerMonitor.dll 路径> [配置目录]
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "include/PluginInterface.h"

typedef ITMPlugin* (*PFN_GetInstance)();

static std::string ToUtf8(const wchar_t* w)
{
    if (w == nullptr)
        return std::string("(null)");
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1)
        return std::string();
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
    return s;
}

static int g_fail = 0;
static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok)
        ++g_fail;
}

int wmain(int argc, wchar_t** argv)
{
    ::SetConsoleOutputCP(65001);

    if (argc < 2)
    {
        std::printf("usage: PluginSmokeTest.exe <PowerMonitor.dll> [config_dir]\n");
        return 2;
    }
    const wchar_t* dll_path = argv[1];
    std::wstring cfg_dir = argc >= 3 ? argv[2] : L"";

    // 配置目录默认取 DLL 同级目录下的 _smokecfg
    if (cfg_dir.empty())
    {
        cfg_dir = dll_path;
        size_t pos = cfg_dir.find_last_of(L"\\/");
        cfg_dir = (pos == std::wstring::npos ? std::wstring(L".") : cfg_dir.substr(0, pos)) + L"\\_smokecfg";
    }
    ::CreateDirectoryW(cfg_dir.c_str(), nullptr);

    std::printf("DLL: %s\nCFG: %s\n\n", ToUtf8(dll_path).c_str(), ToUtf8(cfg_dir.c_str()).c_str());

    HMODULE mod = ::LoadLibraryW(dll_path);
    if (mod == nullptr)
    {
        std::printf("[FAIL] LoadLibrary failed, GetLastError=%lu\n", ::GetLastError());
        return 1;
    }
    std::printf("[ OK ] DLL loaded\n");

    PFN_GetInstance get_instance =
        (PFN_GetInstance)::GetProcAddress(mod, "TMPluginGetInstance");
    Check(get_instance != nullptr, "export TMPluginGetInstance found");
    if (get_instance == nullptr)
        return 1;

    ITMPlugin* plugin = get_instance();
    Check(plugin != nullptr, "TMPluginGetInstance() returned non-null");
    if (plugin == nullptr)
        return 1;

    // ---- 版本 ----
    int api = plugin->GetAPIVersion();
    std::printf("[INFO] GetAPIVersion() = %d\n", api);
    Check(api > 0, "API version > 0");

    // ---- 元信息 ----
    const wchar_t* name = plugin->GetInfo(ITMPlugin::TMI_NAME);
    std::printf("[INFO] TMI_NAME        = %s\n", ToUtf8(name).c_str());
    Check(name != nullptr && name[0] != 0, "TMI_NAME non-empty");
    std::printf("[INFO] TMI_DESCRIPTION = %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_DESCRIPTION)).c_str());
    std::printf("[INFO] TMI_AUTHOR      = %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_AUTHOR)).c_str());
    std::printf("[INFO] TMI_VERSION     = %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_VERSION)).c_str());
    std::printf("[INFO] TMI_URL         = %s\n", ToUtf8(plugin->GetInfo(ITMPlugin::TMI_URL)).c_str());

    // ---- 署名与入口必须指向本项目作者（防止品牌信息回退） ----
    Check(std::wstring(plugin->GetInfo(ITMPlugin::TMI_AUTHOR)) == L"init", "TMI_AUTHOR == init");
    // 精确到仓库地址：「关于 / 项目主页」打开的是项目仓库，而不是作者的个人主页。
    // 只校验 "github.com/IInit" 会放过退化成个人主页的情况（它是仓库 URL 的前缀）。
    Check(std::wstring(plugin->GetInfo(ITMPlugin::TMI_URL)) == L"https://github.com/IInit/PowerMonitor",
          "TMI_URL == https://github.com/IInit/PowerMonitor");
    for (int i = 0; i <= (int)ITMPlugin::TMI_MAX; ++i)
    {
        const wchar_t* v = plugin->GetInfo((ITMPlugin::PluginInfoIndex)i);
        if (v == nullptr)
            continue;
        std::wstring s(v);
        Check(s.find(L"gxpqaznew") == std::wstring::npos, "no upstream author string in GetInfo");
    }

    // ---- 初始化流程（与主程序一致：先 EI_CONFIG_DIR，再 OnInitialize） ----
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, cfg_dir.c_str());
    plugin->OnInitialize(nullptr);
    std::printf("[ OK ] OnExtenedInfo + OnInitialize done\n");

    // ---- 枚举显示项（越界返回 nullptr） ----
    std::printf("\n---- items ----\n");
    int count = 0;
    for (int i = 0; i < 64; ++i)
    {
        IPluginItem* item = plugin->GetItem(i);
        if (item == nullptr)
            break;
        ++count;
        std::printf("%2d | id=%-16s | name=%-14s | label=%-8s | value=%-12s | sample=%s\n",
                    i,
                    ToUtf8(item->GetItemId()).c_str(),
                    ToUtf8(item->GetItemName()).c_str(),
                    ToUtf8(item->GetItemLableText()).c_str(),
                    ToUtf8(item->GetItemValueText()).c_str(),
                    ToUtf8(item->GetItemValueSampleText()).c_str());
    }
    std::printf("item count = %d\n", count);
    Check(count == 15, "item count == 15");
    Check(plugin->GetItem(-1) == nullptr, "GetItem(-1) == nullptr");
    Check(plugin->GetItem(count) == nullptr, "GetItem(count) == nullptr");

    // ---- 采样若干轮 ----
    plugin->DataRequired();
    ::Sleep(1200);
    plugin->DataRequired();

    std::printf("\n---- after DataRequired ----\n");
    for (int i = 0; i < count; ++i)
    {
        IPluginItem* item = plugin->GetItem(i);
        std::printf("%2d | %-8s = %s\n", i,
                    ToUtf8(item->GetItemLableText()).c_str(),
                    ToUtf8(item->GetItemValueText()).c_str());
    }

    const wchar_t* tip = plugin->GetTooltipInfo();
    std::printf("\n---- tooltip ----\n%s\n", ToUtf8(tip).c_str());
    Check(tip != nullptr && tip[0] != 0, "tooltip non-empty");

    // ---- 命令 ----
    int cmds = plugin->GetCommandCount();
    std::printf("\n---- commands (%d) ----\n", cmds);
    for (int i = 0; i < cmds; ++i)
        std::printf("%d: %s  checked=%d\n", i,
                    ToUtf8(plugin->GetCommandName(i)).c_str(),
                    plugin->IsCommandChecked(i));
    Check(cmds == 5, "command count == 5");

    // ---- 图标 ----
    void* icon = plugin->GetPluginIcon();
    std::printf("\n[%s] GetPluginIcon() = %p\n", icon != nullptr ? " OK " : "WARN", icon);

    // ---- 配置是否落盘 ----
    std::wstring cfg_file = cfg_dir + L"\\PowerMonitor_config.json";
    std::wstring state_file = cfg_dir + L"\\PowerMonitor_state.json";
    std::printf("\n[%s] config json written: %s\n",
                ::GetFileAttributesW(cfg_file.c_str()) != INVALID_FILE_ATTRIBUTES ? " OK " : "FAIL",
                ToUtf8(cfg_file.c_str()).c_str());
    std::printf("[%s] state json  written: %s\n",
                ::GetFileAttributesW(state_file.c_str()) != INVALID_FILE_ATTRIBUTES ? " OK " : "WARN",
                ToUtf8(state_file.c_str()).c_str());

    ::FreeLibrary(mod);

    std::printf("\n==== %s (%d failure) ====\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
