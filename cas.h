#ifndef H_CAS_H

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#define _UNICODE

#pragma warning(push, 0)
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <roapi.h>
#include <windowsx.h>
#include <taskschd.h>
#include <ntsecapi.h>
#include <ntstatus.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#pragma warning(pop)

#pragma comment (lib, "kernel32")
#pragma comment (lib, "user32")
#pragma comment (lib, "advapi32")
#pragma comment (lib, "gdi32")
#pragma comment (lib, "shlwapi")
#pragma comment (lib, "shell32")
#pragma comment (lib, "ole32")
#pragma comment (lib, "runtimeobject")
#pragma comment(lib, "taskschd.lib")

#ifdef _DEBUG
#define ASSERT(x) do { if (!(x)) { *(volatile int*)0; } } while (0)
#else
#define ASSERT(x) (void)(x);
#endif

#define ARRAY_COUNT(x)      (sizeof(x) / sizeof(*(x)))

void cas_set_timer(int seconds);
void cas_stop_timer(void);
HRESULT cas_create_admin_task(void);
HRESULT cas_delete_admin_task(void);
void cas_disable_hotkeys(void);
BOOL cas_enable_hotkeys(void);

#define H_CAS_H
#endif
