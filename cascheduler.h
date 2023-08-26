#ifndef H_CASCHEDULER_H

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#define _UNICODE

#pragma warning(push, 0)
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#pragma warning(pop)

#define ASSERT(x) do { if (!(x)) { *(volatile int*)0; } } while (0)
#define ARRAY_COUNT(x)      (sizeof(x) / sizeof(*(x)))

#define H_CASCHEDULER_H
#endif
