// Encoding.cpp : UTF-8 与宽字符互转实现
#include "pch.h"
#include "Encoding.h"

std::wstring Utf8ToWString(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], len);
    return w;
}

std::string WStringToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

CString Utf8ToCString(const std::string& utf8)
{
    return CString(Utf8ToWString(utf8).c_str());
}

std::string CStringToUtf8(const CString& s)
{
    return WStringToUtf8(std::wstring(s));
}
