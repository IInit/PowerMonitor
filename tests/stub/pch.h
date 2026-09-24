// Minimal stub of pch.h / windows.h for g++ -fsyntax-only on non-MFC Windows layers
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned int UINT;
typedef void* LPVOID;
typedef void* HANDLE;
typedef HANDLE HMODULE;
typedef HANDLE HWND;
typedef HANDLE HICON;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef const char* LPCSTR;
typedef char* LPSTR;
typedef unsigned long long ULONGLONG;
typedef ULONGLONG* PULONGLONG;
typedef long LONG;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef long long LONGLONG;

#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define WINAPI
#define WIN32_LEAN_AND_MEAN

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; } u;
    long long QuadPart;
} LARGE_INTEGER;

typedef void (*FARPROC)();
extern "C" {
    HMODULE LoadLibraryW(LPCWSTR);
    HMODULE GetModuleHandleW(LPCWSTR);
    FARPROC GetProcAddress(HMODULE, LPCSTR);
    BOOL FreeLibrary(HMODULE);
    BOOL GetSystemTimes(FILETIME*, FILETIME*, FILETIME*);
    void GetSystemTimeAsFileTime(FILETIME*);
    void GetLocalTime(void*);
    DWORD GetTimeZoneInformation(void*);
    BOOL QueryPerformanceCounter(LARGE_INTEGER*);
    BOOL QueryPerformanceFrequency(LARGE_INTEGER*);
    DWORD GetTickCount64();
}

#define AFX_MANAGE_STATE(x)
