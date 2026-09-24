// Encoding.h : UTF-8 与宽字符 / CString 互转
#pragma once
#include <string>
#include <atlstr.h>

std::wstring Utf8ToWString(const std::string& utf8);
std::string WStringToUtf8(const std::wstring& w);

CString Utf8ToCString(const std::string& utf8);
std::string CStringToUtf8(const CString& s);
