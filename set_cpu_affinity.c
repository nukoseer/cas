#include <string.h>
#include <stdlib.h>

#include "cascheduler.h"
#include "cascheduler_dialog.h"

#pragma comment (lib, "Kernel32")
#pragma comment (lib, "User32")
#pragma comment (lib, "Advapi32")
#pragma comment (lib, "Gdi32")
#pragma comment (lib, "Shlwapi")
#pragma comment (lib, "Shell32")

#define CASCHEDULER_NAME          (L"CPU Affinity Scheduler")
#define CASCHEDULER_PROCESS_NAME  ("cpu_affinity.exe")
#define CASCHEDULER_INI           (L"CPUAffinityScheduler.ini")
#define SETTINGS_FILE_NAME ("settings.txt")
#define MILLISECONDS (1000)
#define TIMER_PERIOD (MILLISECONDS * 5)

#define WM_CASCHEDULER_COMMAND (WM_USER + 0)

static HANDLE timer_handle;
static WCHAR global_ini_path[sizeof(CASCHEDULER_INI)];
static HICON global_icon;

#ifdef _DEBUG
#include <stdio.h>
#include <stdarg.h>

#define DEBUG_FILE_NAME ("debug.txt")

static HANDLE debug_file_handle;

static void write_file(const char* fmt, ...)
{
    DWORD bytes_written = 0;
    va_list args;
    char temp[512] = { 0 };
    int length = 0;

    va_start(args, fmt);
    length = vsnprintf(temp, 511, fmt, args);
    va_end(args);

    if (length > 0)
    {
        WriteFile(debug_file_handle, temp, length, &bytes_written, 0);
    }
}

static void print_bits(const char* label, unsigned int bits)
{
    unsigned int i = 0;
    unsigned int size = sizeof(bits) * 8;
    unsigned int mask = 0x80000000;

    if (label)
        write_file("%s ", label);

    for (i = 0; i < size; ++i)
    {
        write_file("%d", !!(bits & mask));
        mask >>= 1;
    }

    write_file("\n");
}

static void print_affinity_mask(const char* label, unsigned int affinity_mask)
{
    write_file("  %s:\n", label);
    write_file("    Hex   : 0x%X\n", (unsigned int)affinity_mask);
    print_bits("    Binary:", affinity_mask);
}

#else
#define write_file(...)
#define print_bits(...)
#define print_affinity_mask(...)
#endif

typedef struct ProcessAffinity
{
    char name[MAX_PATH];
    unsigned int mask;
} ProcessAffinity;

static ProcessAffinity process_affinities[16];
static unsigned int process_affinity_count;

static char* get_line(char* line, unsigned int* line_size, char* buffer)
{
    char* current = buffer;
    
    while (*current != '\0')
    {
        if (*current == '\n' || *current == '\r')
        {
            break;
        }

        ++current;
    }

    *line_size = (unsigned int)(current - buffer);

    if (*line_size && *line_size < MAX_PATH)
    {
        memcpy(line, buffer, *line_size);
    }
    else
    {
        *line_size = 0;
    }

    if (*current == '\r')
    {
        current += 2;
    }
    
    return current;
}

static void read_settings(void)
{
    HANDLE settings_handle = CreateFile(SETTINGS_FILE_NAME, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD file_size = 0;
    DWORD bytes_read = 0;
    char* file_buffer = 0;

    if (settings_handle != INVALID_HANDLE_VALUE)
    {
        file_size = GetFileSize(settings_handle, 0);
        file_buffer = malloc(file_size + 1);
        memset(file_buffer, 0, file_size + 1);
    }
    else
    {
        write_file("%s could not open!\n", SETTINGS_FILE_NAME);
        ASSERT(0);
    }

    if (file_buffer)
    {
        ReadFile(settings_handle, file_buffer, file_size, &bytes_read, 0);
    }
    else
    {
        write_file("Allocation is failed!\n");
        ASSERT(0);
    }

    if (file_size && file_size == bytes_read)
    {
        unsigned int i = 0;

        for (i = 0; i < ARRAY_COUNT(process_affinities); ++i)
        {
            ProcessAffinity* process_affinity = process_affinities + i;
            char line[MAX_PATH] = { 0 };
            unsigned int line_size = 0;

            file_buffer = get_line(line, &line_size, file_buffer);

            if (line_size > 0)
            {
                memcpy(process_affinity->name, line, line_size);
            }
            else
            {
                break;
            }

            memset(line, 0, MAX_PATH);
            line_size = 0;
            file_buffer = get_line(line, &line_size, file_buffer);

            if (line_size > 0)
            {
                unsigned int mask = 0;

                if (line[0] == '0' && (line[1] == 'x' || line[2] == 'X'))
                {
                    mask = strtol(line, 0, 0);
                }
                else
                {
                    mask = strtol(line, 0, 10);
                }

                if (mask)
                {
                    process_affinity->mask = mask;
                }
                else
                {
                    write_file("Affinity mask format is wrong for %s!", process_affinity->name);
                    ASSERT(0);
                }
            }
            else
            {
                break;
            }

            if (process_affinity->name && process_affinity->mask)
            {
                ++process_affinity_count;
            }
        }
        
        CloseHandle(settings_handle);
    }
    else
    {
        write_file("File could not read!\n");
        ASSERT(0);
    }

    if (!process_affinity_count)
    {
        write_file("%s does not have enough proecss-affinity pairs!", SETTINGS_FILE_NAME);
    }
}

static void enable_debug_privilege(void)
{
    HANDLE token_handle = 0;
    LUID luid = { 0 };
    TOKEN_PRIVILEGES tkp = { 0 };

    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_handle);

    LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid);

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(token_handle, 0, &tkp, sizeof(tkp), NULL, NULL);

    CloseHandle(token_handle);
}

static int find_process_by_name(const char* process_name, PROCESSENTRY32* entry)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (Process32First(snapshot, entry))
    {
        do {
            if (!strcmp(entry->szExeFile, process_name))
            {
                CloseHandle(snapshot);
                return 1;
            }
        } while (Process32Next(snapshot, entry));
    }

    CloseHandle(snapshot);

    return 0;
}

static void set_cpu_affinity(PROCESSENTRY32* entry, ProcessAffinity* process_affinity)
{
    HANDLE handle_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, entry->th32ProcessID);
    DWORD process_affinity_mask = 0;
    DWORD system_affinity_mask = 0;
    unsigned int desired_affinity_mask = process_affinity->mask;
    
    if (handle_process)
    {
        write_file("Setting affinity mask 0x%X for %s:\n", desired_affinity_mask, entry->szExeFile);

        GetProcessAffinityMask(handle_process, (PDWORD_PTR)&process_affinity_mask, (PDWORD_PTR)&system_affinity_mask);

        print_affinity_mask("Initial affinity mask", process_affinity_mask);

        if (process_affinity_mask != desired_affinity_mask)
        {
            SetProcessAffinityMask(handle_process, desired_affinity_mask);
            GetProcessAffinityMask(handle_process, (PDWORD_PTR)&process_affinity_mask, (PDWORD_PTR)&system_affinity_mask);

            if (process_affinity_mask == desired_affinity_mask)
            {
                print_affinity_mask("New affinity mask", desired_affinity_mask);
                write_file("SUCCESSFUL!\n");
            }
            else
            {
                write_file("  Could not set!\n");
            }
        }

        write_file("\n");
        CloseHandle(handle_process);
    }
    else
    {
        write_file("'%s' could not open. Please try to run %s as administrator.\n", CASCHEDULER_PROCESS_NAME, process_affinity->name);
        write_file("FAILED!\n");
    }
}

static void cpu_affinity_routine(void)
{
    unsigned int i = 0;
            
    for (i = 0; i < process_affinity_count; ++i)
    {
        ProcessAffinity* process_affinity = process_affinities + i;
        PROCESSENTRY32 entry = { .dwSize = sizeof(PROCESSENTRY32) };
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (Process32First(snapshot, &entry))
        {
            do
            {
                // write_file("[%s] == [%s]\n", entry.szExeFile, process_affinity->name);
                if (!strcmp(entry.szExeFile, process_affinity->name))
                {
                    set_cpu_affinity(&entry, process_affinity);
                }
            } while (Process32Next(snapshot, &entry));

        }
        CloseHandle(snapshot);
    }
}

static void set_timer(void)
{
    FILETIME file_time = { 0 };
    LARGE_INTEGER due_time = { 0 };
    BOOL is_timer_set = 0;

    GetSystemTimeAsFileTime(&file_time);

    timer_handle = CreateWaitableTimerW(0, 0, 0);
    ASSERT(timer_handle);

    due_time.LowPart = file_time.dwLowDateTime;
    due_time.HighPart = file_time.dwHighDateTime;

    is_timer_set = SetWaitableTimer(timer_handle, &due_time, TIMER_PERIOD, 0, 0, 0);
    ASSERT(is_timer_set);
}

static LRESULT CALLBACK window_proc(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CREATE)
    {
        NOTIFYICONDATAW data =
        {
            .cbSize = sizeof(data),
            .hWnd = window_handle,
            .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
            .uCallbackMessage = WM_CASCHEDULER_COMMAND,
            .hIcon = global_icon,
        };
        StrCpyNW(data.szInfoTitle, CASCHEDULER_NAME, ARRAY_COUNT(data.szInfoTitle));
        Shell_NotifyIconW(NIM_ADD, &data);

        return 0;
    }
    return DefWindowProcW(window_handle, message, wparam, lparam);
}

static HWND window;
static CASchedulerDialogConfig global_dialog_config;

#ifdef _DEBUG
int WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int n_show_cmd)
{
    (void)instance, (void)prev_instance, (void)cmd_line, (void)n_show_cmd;
#else
void WinMainCRTStartup(void)
{
#endif
#ifdef _DEBUG
    debug_file_handle = CreateFile(DEBUG_FILE_NAME, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
#endif

	// PROCESSENTRY32 entry;

    // if (find_process_by_name(CASCHEDULER_PROCESS_NAME, &entry) && GetCurrentProcessId() != entry.th32ProcessID)
    // {
    //     write_file("%s is already running!", CASCHEDULER_PROCESS_NAME);
    //     ExitProcess(0);   
    // }

    WNDCLASSEXW window_class =
	{
		.cbSize = sizeof(window_class),
		.lpfnWndProc = &window_proc,
		.hInstance = GetModuleHandle(0),
		.lpszClassName = CASCHEDULER_NAME,
	};

    HWND existing = FindWindowW(window_class.lpszClassName, NULL);
	if (existing)
	{
		// PostMessageW(Existing, WM_TWITCH_NOTIFY_ALREADY_RUNNING, 0, 0);
		ExitProcess(0);
	}
    
    WCHAR exe_folder[MAX_PATH];
    GetModuleFileNameW(NULL, exe_folder, ARRAY_COUNT(exe_folder));
	PathRemoveFileSpecW(exe_folder);
    PathCombineW(global_ini_path, exe_folder, CASCHEDULER_INI);
	write_file("global_ini_path: %s\n", global_ini_path);

    global_icon = LoadIconW(GetModuleHandleW(0), MAKEINTRESOURCEW(1));
    
    ATOM atom = RegisterClassExW(&window_class);
	ASSERT(atom);

	RegisterWindowMessageW(L"TaskbarCreated");

    window = CreateWindowExW(0, window_class.lpszClassName, window_class.lpszClassName, WS_POPUP,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             NULL, NULL, window_class.hInstance, NULL);
    
    enable_debug_privilege();
    read_settings();
    set_timer();
    cascheduler_dialog_config_load(&global_dialog_config);
    cascheduler_dialog_show(&global_dialog_config);

    for (;;)
	{
        DWORD wait = MsgWaitForMultipleObjects(1, &timer_handle, FALSE, INFINITE, QS_ALLINPUT);
        write_file("wait: %d\n", (int)wait);

		if (wait == WAIT_OBJECT_0)
		{
            cpu_affinity_routine();
 		}
		else if (wait == WAIT_OBJECT_0 + 1)
		{
            MSG message;
            BOOL result = GetMessageW(&message, NULL, 0, 0);

            if (result == 0)
            {
                ExitProcess(0);
            }
            ASSERT(result > 0);

            TranslateMessage(&message);
            DispatchMessageW(&message);
		}
        else
        {
            ASSERT(0);
        }
	}
}
