#include "cascheduler.h"
#include "cascheduler_dialog.h"

#define CASCHEDULER_NAME          (L"CPU Affinity Scheduler")
#define CASCHEDULER_URL           (L"https://github.com/nukoseer/CPUAffinityScheduler")

#define CASCHEDULER_INI           (L"CPUAffinityScheduler.ini")
#define SECONDS_TO_MILLISECONDS   (1000)
#define TIMER_PERIOD              (SECONDS_TO_MILLISECONDS * 5)

#define WM_CASCHEDULER_COMMAND          (WM_USER + 0)
#define WM_CASCHEDULER_ALREADY_RUNNING  (WM_USER + 1)
#define CMD_CASCHEDULER                 (1)
#define CMD_QUIT                        (2)

typedef struct
{
    HWND window_handle;
    HANDLE timer_handle;
    WCHAR ini_path[MAX_PATH];
    HICON icon;
    CASchedulerDialogConfig dialog_config;
} CAScheduler;

static CAScheduler global_cascheduler;

static void cascheduler__enable_debug_privilege(void)
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

static BOOL cascheduler__set_cpu_affinity(PROCESSENTRY32W* entry, unsigned int desired_affinity_mask)
{
    BOOL set = 0;
    HANDLE handle_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, entry->th32ProcessID);
    DWORD process_affinity_mask = 0;
    DWORD system_affinity_mask = 0;

    if (handle_process)
    {
        GetProcessAffinityMask(handle_process, (PDWORD_PTR)&process_affinity_mask, (PDWORD_PTR)&system_affinity_mask);

        if (process_affinity_mask != desired_affinity_mask)
        {
            SetProcessAffinityMask(handle_process, desired_affinity_mask);
            GetProcessAffinityMask(handle_process, (PDWORD_PTR)&process_affinity_mask, (PDWORD_PTR)&system_affinity_mask);

            if (process_affinity_mask == desired_affinity_mask)
            {
                set = 1;
            }
        }
        else
        {
            set = 1;
        }

        CloseHandle(handle_process);
    }

    return set;
}

static void cascheduler__cpu_affinity_routine(CASchedulerDialogConfig* dialog_config)
{
    unsigned int i = 0;

    for (i = 0; i < MAX_ITEMS; ++i)
    {
        WCHAR* process = dialog_config->processes[i];
        BOOL found = 0;

        if (*process)
        {
            PROCESSENTRY32W entry = { .dwSize = sizeof(PROCESSENTRY32W) };
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (!lstrcmpW((LPCWSTR)entry.szExeFile, process))
                    {
                        UINT affinity_mask = dialog_config->affinity_masks[i];
                        dialog_config->sets[i] = cascheduler__set_cpu_affinity(&entry, affinity_mask);
                        found = 1;
                    }
                } while (Process32NextW(snapshot, &entry));

                if (!found)
                {
                    dialog_config->sets[i] = 0;
                }
            }
            CloseHandle(snapshot);
        }
        else
        {
            break;
        }
    }
}

static HANDLE cascheduler__create_timer(void)
{
    HANDLE timer_handle = CreateWaitableTimerW(0, 0, 0);
    ASSERT(timer_handle);

    return timer_handle;
}

static CASCHEDULER_DIALOG_CALLBACK(cascheduler__set_timer)
{
    HANDLE timer_handle = (HANDLE)parameter;
    LARGE_INTEGER due_time = { 0 };
    BOOL is_timer_set = 0;
    FILETIME file_time = { 0 };

    GetSystemTimeAsFileTime(&file_time);

    due_time.LowPart = file_time.dwLowDateTime;
    due_time.HighPart = file_time.dwHighDateTime;

    is_timer_set = SetWaitableTimer(timer_handle, &due_time, TIMER_PERIOD, 0, 0, 0);
    ASSERT(is_timer_set);
}

static CASCHEDULER_DIALOG_CALLBACK(cascheduler__stop_timer)
{
    HANDLE timer_handle = (HANDLE)parameter;
    CancelWaitableTimer(timer_handle);
}

static void cascheduler__show_notification(HWND window_handle, LPCWSTR message, LPCWSTR title, DWORD flags)
{
	NOTIFYICONDATAW data =
	{
		.cbSize = sizeof(data),
		.hWnd = window_handle,
		.uFlags = NIF_INFO | NIF_TIP,
		.dwInfoFlags = flags, // NIIF_INFO, NIIF_WARNING, NIIF_ERROR
	};
	StrCpyNW(data.szTip, CASCHEDULER_NAME, ARRAY_COUNT(data.szTip));
	StrCpyNW(data.szInfo, message, ARRAY_COUNT(data.szInfo));
	StrCpyNW(data.szInfoTitle, title ? title : CASCHEDULER_NAME, ARRAY_COUNT(data.szInfoTitle));
	Shell_NotifyIconW(NIM_MODIFY, &data);
}

static void cascheduler__add_tray_icon(HWND window_handle)
{
    NOTIFYICONDATAW data =
    {
        .cbSize = sizeof(data),
        .hWnd = window_handle,
        .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
        .uCallbackMessage = WM_CASCHEDULER_COMMAND,
        .hIcon = global_cascheduler.icon,
    };
    StrCpyNW(data.szInfoTitle, CASCHEDULER_NAME, ARRAY_COUNT(data.szInfoTitle));
    Shell_NotifyIconW(NIM_ADD, &data);
}

static void cascheduler__remove_tray_icon(HWND window_handle)
{
	NOTIFYICONDATAW data =
	{
		.cbSize = sizeof(data),
		.hWnd = window_handle,
	};
	Shell_NotifyIconW(NIM_DELETE, &data);
}

static LRESULT CALLBACK cascheduler__window_proc(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CREATE)
    {
        cascheduler__add_tray_icon(window_handle);
        return 0;
    }
    else if (message == WM_DESTROY)
	{
		// if (gRecording)
		// {
		// 	StopRecording();
		// }
		cascheduler__remove_tray_icon(window_handle);
		PostQuitMessage(0);

		return 0;
	}
    else if (message == WM_CASCHEDULER_ALREADY_RUNNING)
	{
		cascheduler__show_notification(window_handle, L"CPU Affinity Scheduler is already running!", 0, NIIF_INFO);

		return 0;
	}
    else if (message == WM_CASCHEDULER_COMMAND)
	{
		if (LOWORD(lparam) == WM_LBUTTONUP)
		{
            cascheduler_dialog_show(&global_cascheduler.dialog_config);
		}
        else if (LOWORD(lparam) == WM_RBUTTONUP)
		{
			HMENU menu = CreatePopupMenu();
			ASSERT(menu);

            AppendMenuW(menu, MF_STRING, CMD_CASCHEDULER, CASCHEDULER_NAME);
			AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
			AppendMenuW(menu, MF_STRING, CMD_QUIT, L"Exit");

			POINT mouse;
			GetCursorPos(&mouse);

			SetForegroundWindow(window_handle);
			int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window_handle, NULL);

            if (command == CMD_CASCHEDULER)
			{
				ShellExecuteW(NULL, L"open", CASCHEDULER_URL, NULL, NULL, SW_SHOWNORMAL);
			}
			else if (command == CMD_QUIT)
			{
				DestroyWindow(window_handle);
			}

            DestroyMenu(menu);
		}

        return 0;
    }

    return DefWindowProcW(window_handle, message, wparam, lparam);
}

static DWORD WINAPI cascheduler__timer_thread_proc(LPVOID parameter)
{
    CAScheduler* cascheduler = (CAScheduler*)parameter;
    HANDLE timer_handle = cascheduler->timer_handle;

    for (;;)
    {
        DWORD wait = WaitForSingleObject(timer_handle, INFINITE);

        if (wait == WAIT_OBJECT_0)
        {
            cascheduler__cpu_affinity_routine(&cascheduler->dialog_config);
        }
    }

    return 0;
}

#ifdef _DEBUG
int WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int n_show_cmd)
{
    (void)instance, (void)prev_instance, (void)cmd_line, (void)n_show_cmd;
#else
void WinMainCRTStartup(void)
{
#endif
    WNDCLASSEXW window_class =
	{
		.cbSize = sizeof(window_class),
		.lpfnWndProc = &cascheduler__window_proc,
		.hInstance = GetModuleHandle(0),
		.lpszClassName = CASCHEDULER_NAME,
	};

    HWND existing = FindWindowW(window_class.lpszClassName, NULL);
	if (existing)
	{
		PostMessageW(existing, WM_CASCHEDULER_ALREADY_RUNNING, 0, 0);
		ExitProcess(0);
	}

    WCHAR exe_folder[MAX_PATH];
    GetModuleFileNameW(NULL, exe_folder, ARRAY_COUNT(exe_folder));
	PathRemoveFileSpecW(exe_folder);
    PathCombineW(global_cascheduler.ini_path, exe_folder, CASCHEDULER_INI);

    global_cascheduler.icon = LoadIconW(GetModuleHandleW(0), MAKEINTRESOURCEW(1));

    ATOM atom = RegisterClassExW(&window_class);
	ASSERT(atom);

	RegisterWindowMessageW(L"TaskbarCreated");

    global_cascheduler.window_handle = CreateWindowExW(0, window_class.lpszClassName, window_class.lpszClassName, WS_POPUP,
                                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                                NULL, NULL, window_class.hInstance, NULL);
    global_cascheduler.timer_handle = cascheduler__create_timer();

    HANDLE timer_thread_handle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&cascheduler__timer_thread_proc, (LPVOID)&global_cascheduler, 0, 0);
    ASSERT(timer_thread_handle);
    CloseHandle(timer_thread_handle);

    cascheduler__enable_debug_privilege();
    cascheduler_dialog_config_load(&global_cascheduler.dialog_config, global_cascheduler.ini_path);
    cascheduler_dialog_register_callback(cascheduler__set_timer, global_cascheduler.timer_handle, ID_START);
    cascheduler_dialog_register_callback(cascheduler__stop_timer, global_cascheduler.timer_handle, ID_STOP);
    cascheduler_dialog_show(&global_cascheduler.dialog_config);

    for (;;)
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
}
