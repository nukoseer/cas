#ifndef H_CAS_H

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#define _UNICODE

#pragma warning(push, 0)
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#pragma warning(pop)

#pragma comment (lib, "Kernel32")
#pragma comment (lib, "User32")
#pragma comment (lib, "Advapi32")
#pragma comment (lib, "Gdi32")
#pragma comment (lib, "Shlwapi")
#pragma comment (lib, "Shell32")

#ifdef _DEBUG
#define ASSERT(x) do { if (!(x)) { *(volatile int*)0; } } while (0)
#else
#define ASSERT(x) (void)(x);
#endif

#define ARRAY_COUNT(x)      (sizeof(x) / sizeof(*(x)))

#define H_CAS_H
#endif
