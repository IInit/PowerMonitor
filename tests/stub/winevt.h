// Minimal winevt.h stub
#pragma once
typedef HANDLE EVT_HANDLE;
typedef DWORD EVT_QUERY_FLAGS;
typedef DWORD EVT_RENDER_FLAGS;
typedef DWORD EVT_RENDER_CONTEXT_FLAGS;
typedef DWORD EVT_VARIANT_TYPE;

#define EvtQueryChannelPath          0x1
#define EvtQueryReverseDirection     0x200
#define EvtRenderEventValues         0
#define INFINITE                     0xFFFFFFFF

extern "C" {
    EVT_HANDLE EvtQuery(EVT_HANDLE, LPCWSTR, LPCWSTR, EVT_QUERY_FLAGS);
    BOOL EvtNext(EVT_HANDLE, DWORD, EVT_HANDLE*, DWORD, DWORD, DWORD*);
    BOOL EvtRender(EVT_HANDLE, EVT_HANDLE, EVT_RENDER_FLAGS, DWORD, LPVOID, DWORD*, DWORD*);
    BOOL EvtClose(EVT_HANDLE);
}
