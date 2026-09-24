// Minimal evntcons.h stub (only fields used by PowerOn.cpp)
#pragma once
#include "pch.h"

typedef enum _EVT_VARIANT_TYPE_VALUES {
    EvtVarTypeNull = 0,
    EvtVarTypeBoolean = 1,
    EvtVarTypeFileTime = 17
} EVT_VARIANT_TYPE_VALUES;

typedef struct _EVT_VARIANT {
    union {
        BOOL BooleanVal;
        LONG SByte;
        BYTE ByteVal;
        LONG Int16Val;
        WORD UInt16Val;
        LONG Int32Val;
        DWORD UInt32Val;
        LONGLONG Int64Val;
        ULONGLONG UInt64Val;
        float SingleVal;
        double DoubleVal;
        LPCWSTR StringVal;
        FILETIME FileTime;
        HANDLE SysTime;
    };
    DWORD Count;
    DWORD Type;
} EVT_VARIANT;
