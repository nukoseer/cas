#include "cas.h"
#include "cas_dialog.h"

#define CAS_NAME                  (L"cas")
#define CAS_URL                   (L"https://github.com/nukoseer/cas")
#define CAS_INI                   (L"cas.ini")

#define WM_CAS_COMMAND            (WM_USER + 0)
#define WM_CAS_ALREADY_RUNNING    (WM_USER + 1)
#define CMD_CAS                   (1)
#define CMD_QUIT                  (2)
#define HOT_MENU                  (13)

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

static BOOL cas__get_psid(PSID* psid)
{
    BOOL result = FALSE;
    HANDLE token_handle = 0;
    char token_information[64] = { 0 };
    DWORD return_length = 0;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_handle))
    {
        if (GetTokenInformation(token_handle, TokenUser, token_information, sizeof(token_information), &return_length))
        {
            TOKEN_USER* token_user = (TOKEN_USER*)token_information;
            *psid = token_user->User.Sid;
            result = TRUE;
        }

        CloseHandle(token_handle);
    }

    return result;
}

static LSA_HANDLE cas__get_lookup_policy_handle(void)
{
    LSA_OBJECT_ATTRIBUTES object_attributes = { 0 };
    LSA_HANDLE policy_handle = 0;

    LsaOpenPolicy(0, &object_attributes, POLICY_LOOKUP_NAMES, &policy_handle);

    return policy_handle;
}

static BOOL cas__get_sid_full_name(PSID psid, WCHAR* full_name)
{
    BOOL result = FALSE;
    NTSTATUS status = 0;
    LSA_HANDLE policy_handle = 0;
    PLSA_REFERENCED_DOMAIN_LIST referenced_domains = 0;
    PLSA_TRANSLATED_NAME names = 0;

    policy_handle = cas__get_lookup_policy_handle();

    if (policy_handle)
    {
        status = LsaLookupSids(policy_handle, 1, &psid, &referenced_domains, &names);

        if (STATUS_SUCCESS == status)
        {
            if (names[0].Use != SidTypeInvalid && names[0].Use != SidTypeUnknown)
            {
                PWSTR domain_name_buffer = 0;
                ULONG domain_name_length = 0;

                if (names[0].DomainIndex >= 0)
                {
                    PLSA_TRUST_INFORMATION trustInfo;

                    trustInfo = &referenced_domains->Domains[names[0].DomainIndex];
                    domain_name_buffer = trustInfo->Name.Buffer;
                    domain_name_length = trustInfo->Name.Length;
                }

                if (domain_name_buffer && domain_name_length != 0)
                {
                    memcpy(full_name, domain_name_buffer, domain_name_length);
                    full_name[domain_name_length / sizeof(WCHAR)] = L'\\';
                    memcpy(full_name + domain_name_length / sizeof(WCHAR) + 1, names[0].Name.Buffer, names[0].Name.Length);
                    result = TRUE;
                }
                else
                {
                    memcpy(full_name, &names[0].Name, names[0].Name.Length);
                    result = TRUE;
                }
            }
        }

        if (referenced_domains)
            LsaFreeMemory(referenced_domains);
        if (names)
            LsaFreeMemory(names);
    }

    return result;
}

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
        BOOL found = FALSE;

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
                        dialog_config->dones[i] = cas__set_cpu_affinity(&entry, affinity_mask);
                        found = TRUE;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            
            if (!found)
            {
                dialog_config->dones[i] = 0;
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

    unsigned int max_retries = 5;

    for (unsigned int i = 0; i < max_retries; ++i)
    {
        if (Shell_NotifyIconW(NIM_ADD, &data) == TRUE)
        {
            break;
        }

        Sleep(200);
    }
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
    else if (message == WM_HOTKEY)
	{
        if (wparam == HOT_MENU)
        {
            cas_dialog_show(&global_cas.dialog_config);
        }
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

// NOTE: https://github.com/winsiderss/systeminformer/blob/5d97d6b3f99bd7c651b448ae414f39150cf9af2f/SystemInformer/admintask.c#L22
HRESULT cas_create_admin_task(void)
{
    HRESULT status = E_FAIL;
    WCHAR* task_time_limit = L"PT0S";
    VARIANT empty = { VT_EMPTY };
    ITaskService* task_service = 0;
    ITaskFolder* task_folder = 0;
    ITaskDefinition* task_definition = 0;
    ITaskSettings* task_settings = 0;
    ITaskSettings2* task_settings2 = 0;
    ITriggerCollection* task_trigger_collection = 0;
    ITrigger* task_trigger = 0;
    ILogonTrigger* task_logon_trigger = 0;
    IRegisteredTask* task_registered_task = 0;
    IPrincipal* task_principal = 0;
    IActionCollection* task_action_collection = 0;
    IAction* task_action = 0;
    IExecAction* task_exec_action = 0;
    WCHAR exe_path[MAX_PATH] = { 0 };
    WCHAR domain_user_name[64] = { 0 };
    PSID psid = { 0 };

    GetModuleFileNameW(NULL, exe_path, ARRAY_COUNT(exe_path));

    status = CoCreateInstance(&CLSID_TaskScheduler, 0, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&task_service);

    if (FAILED(status))
    {
        CoUninitialize();
        goto cleanup_exit;
    }

    status = ITaskService_Connect(task_service, empty, empty, empty, empty);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITaskService_GetFolder(task_service, L"\\", &task_folder);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITaskService_NewTask(task_service, 0, &task_definition);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITaskDefinition_get_Settings(task_definition, &task_settings);

    if (FAILED(status))
        goto cleanup_exit;

    ITaskSettings_put_Compatibility(task_settings, TASK_COMPATIBILITY_V2_1);
    ITaskSettings_put_StartWhenAvailable(task_settings, VARIANT_TRUE);
    ITaskSettings_put_DisallowStartIfOnBatteries(task_settings, VARIANT_FALSE);
    ITaskSettings_put_StopIfGoingOnBatteries(task_settings, VARIANT_FALSE);
    ITaskSettings_put_ExecutionTimeLimit(task_settings, task_time_limit);
    ITaskSettings_put_Priority(task_settings, 1);

    if (SUCCEEDED(ITaskSettings_QueryInterface(task_settings, &IID_ITaskSettings2, &task_settings2)))
    {
        ITaskSettings2_put_UseUnifiedSchedulingEngine(task_settings2, VARIANT_TRUE);
        ITaskSettings2_put_DisallowStartOnRemoteAppSession(task_settings2, VARIANT_TRUE);
        ITaskSettings2_Release(task_settings2);
    }

    status = ITaskDefinition_get_Triggers(task_definition, &task_trigger_collection);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITriggerCollection_Create(task_trigger_collection, TASK_TRIGGER_LOGON, &task_trigger);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITrigger_QueryInterface(task_trigger, &IID_ILogonTrigger, &task_logon_trigger);

    if (FAILED(status))
        goto cleanup_exit;

    ILogonTrigger_put_Id(task_logon_trigger, L"LogonTriggerId");

    if (!cas__get_psid(&psid) || !cas__get_sid_full_name(psid, domain_user_name))
        goto cleanup_exit;

    ILogonTrigger_put_UserId(task_logon_trigger, domain_user_name);

    status = ITaskDefinition_get_Principal(task_definition, &task_principal);

    if (FAILED(status))
        goto cleanup_exit;

    IPrincipal_put_RunLevel(task_principal, TASK_RUNLEVEL_HIGHEST);
    IPrincipal_put_LogonType(task_principal, TASK_LOGON_INTERACTIVE_TOKEN);

    status = ITaskDefinition_get_Actions(task_definition, &task_action_collection);

    if (FAILED(status))
        goto cleanup_exit;

    status = IActionCollection_Create(task_action_collection, TASK_ACTION_EXEC, &task_action);

    if (FAILED(status))
        goto cleanup_exit;

    status = IAction_QueryInterface(task_action, &IID_IExecAction, &task_exec_action);

    if (FAILED(status))
        goto cleanup_exit;

    status = IExecAction_put_Path(task_exec_action, exe_path);

    if (FAILED(status))
        goto cleanup_exit;

    ITaskFolder_DeleteTask(task_folder, CAS_NAME, 0);

    status = ITaskFolder_RegisterTaskDefinition(task_folder, CAS_NAME, task_definition,
                                                TASK_CREATE_OR_UPDATE, empty, empty,
                                                TASK_LOGON_INTERACTIVE_TOKEN, empty,
                                                &task_registered_task);

cleanup_exit:

    if (task_registered_task)
        IRegisteredTask_Release(task_registered_task);
    if (task_action_collection)
        IActionCollection_Release(task_action_collection);
    if (task_principal)
        IPrincipal_Release(task_principal);
    if (task_logon_trigger)
        ILogonTrigger_Release(task_logon_trigger);
    if (task_trigger)
        ITrigger_Release(task_trigger);
    if (task_trigger_collection)
        ITriggerCollection_Release(task_trigger_collection);
    if (task_settings)
        ITaskSettings_Release(task_settings);
    if (task_definition)
        ITaskDefinition_Release(task_definition);
    if (task_folder)
        ITaskFolder_Release(task_folder);
    if (task_service)
        ITaskService_Release(task_service);

    return status;
}

HRESULT cas_delete_admin_task(void)
{
    HRESULT status = E_FAIL;
    VARIANT empty = { VT_EMPTY };
    ITaskService* task_service = 0;
    ITaskFolder* task_folder = 0;

    status = CoCreateInstance(&CLSID_TaskScheduler, 0, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&task_service);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITaskService_Connect(task_service, empty, empty, empty, empty);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITaskService_GetFolder(task_service, L"\\", &task_folder);

    if (FAILED(status))
        goto cleanup_exit;

    status = ITaskFolder_DeleteTask(task_folder, CAS_NAME, 0);

cleanup_exit:

    if (task_folder)
        ITaskFolder_Release(task_folder);
    if (task_service)
        ITaskService_Release(task_service);

    return status;
}

void cas_disable_hotkeys(void)
{
	UnregisterHotKey(global_cas.window_handle, HOT_MENU);
}

BOOL cas_enable_hotkeys(void)
{
	BOOL success = TRUE;
    
	if (global_cas.dialog_config.menu_shortcut)
	{
		success = success && RegisterHotKey(global_cas.window_handle, HOT_MENU, HOT_GET_MOD(global_cas.dialog_config.menu_shortcut), HOT_GET_KEY(global_cas.dialog_config.menu_shortcut));
	}

	return success;
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

    CoInitializeEx(0, COINIT_MULTITHREADED);
    CoInitializeSecurity(0, -1, 0, 0, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, 0, 0, 0);

    cas__create_shortcut_link();
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
