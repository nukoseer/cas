#include "cas.h"
#include "cas_dialog.h"

#define CAS_NAME                  (L"cas")
#define CAS_URL                   (L"https://github.com/nukoseer/cas")
#define CAS_INI                   (L"cas.ini")

#define WM_CAS_COMMAND            (WM_USER + 0)
#define WM_CAS_ALREADY_RUNNING    (WM_USER + 1)
#define CMD_CAS                   (1)
#define CMD_QUIT                  (2)

#define SECONDS_TO_MILLISECONDS   (1000)

typedef struct
{
    HWND window_handle;
    HANDLE timer_handle;
    WCHAR ini_path[MAX_PATH];
    HICON icon;
    CasDialogConfig dialog_config;
} Cas;

static Cas global_cas;

// NOTE: https://learn.microsoft.com/en-us/windows/win32/shell/how-to-add-shortcuts-to-the-start-menu
static void cas__create_shortcut_link(void)
{
    HRESULT handle_result = 0;
    WCHAR link_path[MAX_PATH];

    RoInitialize(RO_INIT_SINGLETHREADED);

    GetEnvironmentVariableW(L"APPDATA", link_path, ARRAY_COUNT(link_path));
    PathAppendW(link_path, L"Microsoft\\Windows\\Start Menu\\Programs");
    PathAppendW(link_path, CAS_NAME);
    StrCatW(link_path, L".lnk");

    if (GetFileAttributesW(link_path) == INVALID_FILE_ATTRIBUTES)
    {
        IShellLinkW* shell_link;

        handle_result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (LPVOID*)&shell_link);

        if (SUCCEEDED(handle_result))
        {
            IPersistFile* persist_file;
            WCHAR exe_path[MAX_PATH];

            GetModuleFileNameW(NULL, exe_path, ARRAY_COUNT(exe_path));

            IShellLinkW_SetPath(shell_link, exe_path);
            PathRemoveFileSpecW(exe_path);
            IShellLinkW_SetWorkingDirectory(shell_link, exe_path);

            handle_result = IShellLinkW_QueryInterface(shell_link, &IID_IPersistFile, (LPVOID*)&persist_file);

            if (SUCCEEDED(handle_result))
            {
                handle_result = IPersistFile_Save(persist_file, link_path, TRUE);
                IPersistFile_Release(persist_file);
            }

            IShellLinkW_Release(shell_link);
        }
    }
}

static void cas__enable_debug_privilege(void)
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

static BOOL cas__set_cpu_affinity(PROCESSENTRY32W* entry, unsigned int desired_affinity_mask)
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

static void cas__cpu_affinity_routine(CasDialogConfig* dialog_config)
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
                        found = 1;
                    }

                    if (found && !dialog_config->dones[i])
                    {
                        UINT affinity_mask = dialog_config->affinity_masks[i];
                        dialog_config->dones[i] = cas__set_cpu_affinity(&entry, affinity_mask);
                    }
                } while (Process32NextW(snapshot, &entry));

                if (!found)
                {
                    dialog_config->dones[i] = 0;
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

static HANDLE cas__create_timer(void)
{
    HANDLE timer_handle = CreateWaitableTimerW(0, 0, 0);
    ASSERT(timer_handle);

    return timer_handle;
}

static void cas__show_notification(HWND window_handle, LPCWSTR message, LPCWSTR title, DWORD flags)
{
	NOTIFYICONDATAW data =
	{
		.cbSize = sizeof(data),
		.hWnd = window_handle,
		.uFlags = NIF_INFO | NIF_TIP,
		.dwInfoFlags = flags, // NIIF_INFO, NIIF_WARNING, NIIF_ERROR
	};
	StrCpyNW(data.szTip, CAS_NAME, ARRAY_COUNT(data.szTip));
	StrCpyNW(data.szInfo, message, ARRAY_COUNT(data.szInfo));
	StrCpyNW(data.szInfoTitle, title ? title : CAS_NAME, ARRAY_COUNT(data.szInfoTitle));
	Shell_NotifyIconW(NIM_MODIFY, &data);
}

static void cas__add_tray_icon(HWND window_handle)
{
    NOTIFYICONDATAW data =
    {
        .cbSize = sizeof(data),
        .hWnd = window_handle,
        .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
        .uCallbackMessage = WM_CAS_COMMAND,
        .hIcon = global_cas.icon,
    };
    StrCpyNW(data.szInfoTitle, CAS_NAME, ARRAY_COUNT(data.szInfoTitle));
    Shell_NotifyIconW(NIM_ADD, &data);
}

static void cas__remove_tray_icon(HWND window_handle)
{
	NOTIFYICONDATAW data =
	{
		.cbSize = sizeof(data),
		.hWnd = window_handle,
	};
	Shell_NotifyIconW(NIM_DELETE, &data);
}

static LRESULT CALLBACK cas__window_proc(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CREATE)
    {
        cas__add_tray_icon(window_handle);
        return 0;
    }
    else if (message == WM_DESTROY)
	{
		cas__remove_tray_icon(window_handle);
		PostQuitMessage(0);

		return 0;
	}
    else if (message == WM_CAS_ALREADY_RUNNING)
	{
		cas__show_notification(window_handle, L"cas is already running!", 0, NIIF_INFO);

		return 0;
	}
    else if (message == WM_CAS_COMMAND)
	{
		if (LOWORD(lparam) == WM_LBUTTONUP)
		{
            cas_dialog_show(&global_cas.dialog_config);
		}
        else if (LOWORD(lparam) == WM_RBUTTONUP)
		{
			HMENU menu = CreatePopupMenu();
			ASSERT(menu);

            AppendMenuW(menu, MF_STRING, CMD_CAS, CAS_NAME);
			AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
			AppendMenuW(menu, MF_STRING, CMD_QUIT, L"Exit");

			POINT mouse;
			GetCursorPos(&mouse);

			SetForegroundWindow(window_handle);
			int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window_handle, NULL);

            if (command == CMD_CAS)
			{
				ShellExecuteW(NULL, L"open", CAS_URL, NULL, NULL, SW_SHOWNORMAL);
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

static DWORD WINAPI cas__timer_thread_proc(LPVOID parameter)
{
    Cas* cas = (Cas*)parameter;
    HANDLE timer_handle = cas->timer_handle;

    for (;;)
    {
        DWORD wait = WaitForSingleObject(timer_handle, INFINITE);

        if (wait == WAIT_OBJECT_0)
        {
            cas__cpu_affinity_routine(&cas->dialog_config);
        }
    }

    return 0;
}

void cas_set_timer(int seconds)
{
    LARGE_INTEGER due_time = { 0 };
    BOOL is_timer_set = 0;
    FILETIME file_time = { 0 };

    GetSystemTimeAsFileTime(&file_time);

    due_time.LowPart = file_time.dwLowDateTime;
    due_time.HighPart = file_time.dwHighDateTime;

    is_timer_set = SetWaitableTimer(global_cas.timer_handle, &due_time, seconds * SECONDS_TO_MILLISECONDS, 0, 0, 0);
    ASSERT(is_timer_set);
}

void cas_stop_timer(void)
{
    CancelWaitableTimer(global_cas.timer_handle);
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
		.lpfnWndProc = &cas__window_proc,
		.hInstance = GetModuleHandle(0),
		.lpszClassName = CAS_NAME,
	};

    HWND existing = FindWindowW(window_class.lpszClassName, NULL);
	if (existing)
	{
		PostMessageW(existing, WM_CAS_ALREADY_RUNNING, 0, 0);
		ExitProcess(0);
	}

    ATOM atom = RegisterClassExW(&window_class);
	ASSERT(atom);

	RegisterWindowMessageW(L"TaskbarCreated");

    global_cas.icon = LoadIconW(GetModuleHandleW(0), MAKEINTRESOURCEW(1));
    global_cas.window_handle = CreateWindowExW(0, window_class.lpszClassName, window_class.lpszClassName, WS_POPUP,
                                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                                NULL, NULL, window_class.hInstance, NULL);
    global_cas.timer_handle = cas__create_timer();

    CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&cas__timer_thread_proc, (LPVOID)&global_cas, 0, 0));

    WCHAR exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, ARRAY_COUNT(exe_path));
    PathRemoveFileSpecW(exe_path);
    PathCombineW(global_cas.ini_path, exe_path, CAS_INI);

    cas__create_shortcut_link();
    cas__enable_debug_privilege();
    cas_dialog_init(&global_cas.dialog_config, global_cas.ini_path, global_cas.icon);

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
