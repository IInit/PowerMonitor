// FileUtil.cpp : 跨平台文件读写工具实现
#include "FileUtil.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

static std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], len);
    return w;
}
#endif

namespace FileUtil
{
    bool readFile(const std::string& utf8_path, std::string& content)
    {
        content.clear();
#ifdef _WIN32
        std::wstring wpath = Utf8ToWide(utf8_path);
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, wpath.c_str(), L"rb") != 0 || fp == nullptr)
            return false;
#else
        FILE* fp = std::fopen(utf8_path.c_str(), "rb");
        if (fp == nullptr)
            return false;
#endif
        char buf[8192];
        size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0)
            content.append(buf, n);
        bool ok = std::ferror(fp) == 0;
        std::fclose(fp);
        return ok;
    }

    bool writeFileAtomic(const std::string& utf8_path, const std::string& content)
    {
        std::string tmp_path = utf8_path + ".tmp";
#ifdef _WIN32
        std::wstring wtmp = Utf8ToWide(tmp_path);
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, wtmp.c_str(), L"wb") != 0 || fp == nullptr)
            return false;
#else
        FILE* fp = std::fopen(tmp_path.c_str(), "wb");
        if (fp == nullptr)
            return false;
#endif
        bool ok = true;
        if (!content.empty())
            ok = std::fwrite(content.data(), 1, content.size(), fp) == content.size();
        std::fclose(fp);
        if (!ok)
        {
            std::remove(tmp_path.c_str());
            return false;
        }

#ifdef _WIN32
        std::wstring wdst = Utf8ToWide(utf8_path);
        // 目标文件可能存在，MoveFileExW + MOVEFILE_REPLACE_EXISTING 做原子替换
        if (!::MoveFileExW(wtmp.c_str(), wdst.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            ::DeleteFileW(wtmp.c_str());
            return false;
        }
#else
        if (std::rename(tmp_path.c_str(), utf8_path.c_str()) != 0)
        {
            std::remove(tmp_path.c_str());
            return false;
        }
#endif
        return true;
    }

    bool exists(const std::string& utf8_path)
    {
#ifdef _WIN32
        std::wstring wpath = Utf8ToWide(utf8_path);
        DWORD attr = ::GetFileAttributesW(wpath.c_str());
        return attr != INVALID_FILE_ATTRIBUTES;
#else
        FILE* fp = std::fopen(utf8_path.c_str(), "rb");
        if (fp == nullptr)
            return false;
        std::fclose(fp);
        return true;
#endif
    }
}
