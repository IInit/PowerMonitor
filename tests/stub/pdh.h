// Minimal pdh.h stub
#pragma once
typedef HANDLE PDH_HQUERY;
typedef HANDLE PDH_HCOUNTER;
typedef LONG PDH_STATUS;

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

#define PDH_FMT_DOUBLE   0x00000200
#define PDH_FMT_NOSCALE  0x00001000
#define PDH_FMT_NOCAP100 0x00008000

typedef struct _PDH_FMT_COUNTERVALUE {
    DWORD CStatus;
    union {
        LONG longValue;
        double doubleValue;
        LPCWSTR wideStringValue;
    };
} PDH_FMT_COUNTERVALUE;

extern "C" {
    PDH_STATUS PdhOpenQueryW(LPCWSTR, void*, PDH_HQUERY*);
    PDH_STATUS PdhAddEnglishCounterW(PDH_HQUERY, LPCWSTR, DWORD, PDH_HCOUNTER*);
    PDH_STATUS PdhAddCounterW(PDH_HQUERY, LPCWSTR, DWORD, PDH_HCOUNTER*);
    PDH_STATUS PdhCollectQueryData(PDH_HQUERY);
    PDH_STATUS PdhGetFormattedCounterValue(PDH_HCOUNTER, DWORD, void*, PDH_FMT_COUNTERVALUE*);
    PDH_STATUS PdhCloseQuery(PDH_HQUERY);
}
