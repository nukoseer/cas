// NOTE: Source: https://github.com/mmozeiko/wcap/blob/main/wcap_config.c

#include "cas.h"
#include "cas_dialog.h"

#define COL_WIDTH    (150)
#define COL2_WIDTH   (26)
#define ROW_HEIGHT   ((MAX_ITEMS + 1) * ITEM_HEIGHT)
#define ROW2_HEIGHT  ((3 + 1) * ITEM_HEIGHT)
#define BUTTON_WIDTH (50)
#define ITEM_HEIGHT  (14)
#define PADDING      (4)

#define ID_PERIOD         (10)
#define ID_SILENT_START   (11)
#define ID_AUTO_START     (12)
#define ID_PROCESS        (100)
#define ID_AFFINITY_MASK  (200)
#define ID_SET            (300)
#define ID_VALUE_TYPE     (400)
#define ID_VALUE          (500)
#define ID_RESULT         (600)

#define ITEM_CHECKBOX     (1 << 0)
#define ITEM_NUMBER       (1 << 1)
#define ITEM_STRING       (1 << 2)
#define ITEM_CONST_STRING (1 << 3)
#define ITEM_LABEL        (1 << 4)
#define ITEM_COMBOBOX     (1 << 5)
#define ITEM_CENTER       (1 << 6)

#define CONTROL_BUTTON    (0x0080)
#define CONTROL_EDIT      (0x0081)
#define CONTROL_STATIC    (0x0082)
#define CONTROL_COMBOBOX  (0x0085)

#define CAS_DIALOG_INI_PAIRS_SECTION    (L"pairs")
#define CAS_DIALOG_INI_SETTINGS_SECTION (L"settings")

#define CAS_DIALOG_INI_SILENT_START_KEY (L"silent-start")
#define CAS_DIALOG_INI_AUTO_START_KEY   (L"auto-start")
#define CAS_DIALOG_INI_PERIOD_KEY       (L"period")

#define CAS_DIALOG_TIMER_HANDLE_ID      (1)

typedef struct
{
	int left;
	int top;
	int width;
	int height;
} CasDialogRect;

typedef struct
{
	const char* text;
	WORD id;
	WORD item;
	DWORD width;
} CasDialogItem;

typedef struct
{
	const char* caption;
	CasDialogRect rect;
	CasDialogItem items[MAX_ITEMS];
} CasDialogGroup;

typedef struct
{
	CasDialogGroup* groups;
    const char* title;
	const char* font;
    WORD font_size;
} CasDialogLayout;

static BOOL global_is_elavated;
static SYSTEM_INFO global_system_info;
static HWND global_dialog_window;
static WCHAR* global_ini_path;
static HICON global_icon;
static int global_started;
static int global_value_type;
static const char global_check_mark[] = "\x20\x00\x20\x00\x20\x00\x20\x00\x13\x27\x00\x00"; // NOTE: Four space and check mark for easy printing.

static BOOL cas__is_elavated(void)
{
    BOOL result = FALSE;
    HANDLE token_handle = NULL;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_handle))
    {
        TOKEN_ELEVATION elevation;
        DWORD cb_size = sizeof(TOKEN_ELEVATION);

        if (GetTokenInformation(token_handle, TokenElevation, &elevation, sizeof(elevation), &cb_size))
        {
            result = elevation.TokenIsElevated;
        }

        CloseHandle(token_handle);
    }

    return result;
}

static int cas_dialog__validate_bits(const WCHAR* bits, int length, const WCHAR** wrong_bit)
{
    int result = 1;

    for (int i = 0; i < length; ++i)
    {
        if ((bits[i] != L'0' && bits[i] != L'1'))
        {
            result = 0;
            *wrong_bit = bits + i;
            break;
        }
    }

    return result;
}

static unsigned int cas_dialog__bits_to_integer(const WCHAR* bits, int length)
{
    unsigned int result = 0;
    int i;

    ASSERT(length <= 32);

    for (i = 0; i < length; ++i)
    {
        int char_bit = bits[i] - '0';

        result += (1 << (length - i - 1)) * char_bit;
    }

    return result;
}

static int cas_dialog__is_hex(WCHAR digit)
{
    int result = ((digit >= L'0' && digit <= L'9') ||
                  (digit >= L'a' && digit <= L'f')  ||
                  (digit >= L'A' && digit <= L'F'));

    return result;
}

static int cas_dialog__validate_hex(const WCHAR* hex, int length, const WCHAR** wrong_hex)
{
    int result = 1;

    for (int i = 0; i < length; ++i)
    {
        if (!cas_dialog__is_hex(hex[i]))
        {
            result = 0;
            *wrong_hex = hex + i;
            break;
        }
    }

    return result;
}

static void cas_dialog__integer_to_bits(WCHAR* bits, unsigned long long integer, int length)
{
    int i;

    for (i = 0; i < length; ++i)
    {
        bits[i] = (unsigned long long)(1 << (length - i - 1)) & integer ? L'1' : L'0';
    }
}

static void cas_dialog__set_values(HWND window, CasDialogConfig* dialog_config)
{
    for (unsigned int i = 0; i < MAX_ITEMS; ++i)
    {
        SetDlgItemTextW(window, ID_PROCESS + i, dialog_config->processes[i]);

        if (dialog_config->affinity_masks[i])
        {
            WCHAR hex_value_string[32] = { 0 };
            _snwprintf(hex_value_string, ARRAY_COUNT(hex_value_string), L"%X", dialog_config->affinity_masks[i]);
            SetDlgItemTextW(window, ID_AFFINITY_MASK + i, hex_value_string);
        }
        else
        {
            SetDlgItemTextW(window, ID_AFFINITY_MASK + i, L"");
        }

        SetDlgItemTextW(window, ID_SET + i, dialog_config->dones[i] ? (WCHAR*)global_check_mark : L"");
    }

    HWND control = GetDlgItem(window, ID_VALUE_TYPE);
    ComboBox_SetCurSel(control, 0);

    UINT silent_start = GetPrivateProfileIntW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_SILENT_START_KEY, 0, global_ini_path);
    CheckDlgButton(window, ID_SILENT_START, silent_start);

    UINT auto_start = GetPrivateProfileIntW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_AUTO_START_KEY, 0, global_ini_path);
    CheckDlgButton(window, ID_AUTO_START, auto_start);

    UINT period = GetPrivateProfileIntW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_PERIOD_KEY, 5, global_ini_path);
    SetDlgItemInt(window, ID_PERIOD, period, FALSE);
}

static void cas_dialog__convert_value(HWND window)
{
    WCHAR value_string[64] = { 0 };
    WCHAR result_string[64] = { 0 };
    int value_length = 0;

    value_length = GetDlgItemTextW(window, ID_VALUE, value_string, ARRAY_COUNT(value_string));

    if (value_length > 0)
    {
        // NOTE: Bit value
        if (global_value_type == 0)
        {
            WCHAR* wrong_bit = 0;

            if (value_length > 32)
            {
                value_string[32] = '\0';
                SetDlgItemTextW(window, ID_VALUE, value_string);
                SendDlgItemMessageW(window, ID_VALUE, EM_SETSEL, 32, 32);
            }
            else if (cas_dialog__validate_bits(value_string, value_length, &wrong_bit))
            {
                unsigned int result = cas_dialog__bits_to_integer(value_string, value_length);

                _snwprintf(result_string, ARRAY_COUNT(result_string), L"%X", result);
                SetDlgItemTextW(window, ID_RESULT, result_string);
            }
            else
            {
                if (wrong_bit)
                {
                    *wrong_bit = '\0';
                }

                SetDlgItemTextW(window, ID_VALUE, value_string);
                SendDlgItemMessageW(window, ID_VALUE, EM_SETSEL, (WPARAM)value_length - 1, (LPARAM)value_length - 1);
            }
        }
        // NOTE: Hex value
        else if (global_value_type == 1)
        {
            WCHAR* wrong_hex = 0;

            if (value_length > 8)
            {
                value_string[8] = '\0';
                SetDlgItemTextW(window, ID_VALUE, value_string);
                SendDlgItemMessageW(window, ID_VALUE, EM_SETSEL, 16, 16);
            }
            else if (cas_dialog__validate_hex(value_string, value_length, &wrong_hex))
            {
                long long integer = wcstoll(value_string, 0, 16);

                cas_dialog__integer_to_bits(result_string, integer, 32);
                SetDlgItemTextW(window, ID_RESULT, result_string);
            }
            else
            {
                if (wrong_hex)
                {
                    *wrong_hex = '\0';
                }

                SetDlgItemTextW(window, ID_VALUE, value_string);
                SendDlgItemMessageW(window, ID_VALUE, EM_SETSEL, (WPARAM)value_length - 1, (LPARAM)value_length - 1);
            }
        }
    }
    else
    {
        SetDlgItemTextW(window, ID_RESULT, L"");
    }
}

static void cas_dialog__config_save(HWND window)
{
    WritePrivateProfileStringW(CAS_DIALOG_INI_PAIRS_SECTION, 0, L"", global_ini_path);

    // NOTE: We do reverse iteration because WritePrivateProfileSectionW insert names to beginning not to end.
    for (int i = MAX_ITEMS - 1; i >= 0; --i)
    {
        WCHAR process_string[64] = { 0 };
        WCHAR affinity_mask_string[64] = { 0 };
        UINT process_length = 0;
        UINT affinity_mask_length = 0;

        process_length = GetDlgItemTextW(window, ID_PROCESS + i, process_string, ARRAY_COUNT(process_string));
        affinity_mask_length = GetDlgItemTextW(window, ID_AFFINITY_MASK + i, affinity_mask_string, ARRAY_COUNT(affinity_mask_string));

        if (process_length && affinity_mask_length)
        {
            WCHAR pair_string[128] = { 0 };
            int length = process_length;

            memcpy(pair_string, process_string, length * sizeof(WCHAR));
            pair_string[length++] = L':';
            memcpy(pair_string + length, affinity_mask_string, affinity_mask_length * sizeof(WCHAR));
            WritePrivateProfileSectionW(CAS_DIALOG_INI_PAIRS_SECTION, pair_string, global_ini_path);
        }
    }
}

static LRESULT CALLBACK cas_dialog__proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG)
	{
        CasDialogConfig* dialog_config = (CasDialogConfig*)lparam;

        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)dialog_config);

        SendMessage(window, WM_SETICON, ICON_BIG, (LPARAM)global_icon);
        SendDlgItemMessageW(window, ID_VALUE_TYPE, CB_ADDSTRING, 0, (LPARAM)L"Bit");
		SendDlgItemMessageW(window, ID_VALUE_TYPE, CB_ADDSTRING, 0, (LPARAM)L"Hex");

        cas_dialog__set_values(window, dialog_config);

        if (global_started)
        {
            for (unsigned int i = 0; i < MAX_ITEMS; ++i)
            {
                EnableWindow(GetDlgItem(window, ID_PROCESS + i), 0);
                EnableWindow(GetDlgItem(window, ID_AFFINITY_MASK + i), 0);
            }

            EnableWindow(GetDlgItem(window, ID_PERIOD), 0);
            
            SetTimer(window, CAS_DIALOG_TIMER_HANDLE_ID, 1000, 0);
        }

        if (!global_is_elavated)
        {
            EnableWindow(GetDlgItem(window, ID_AUTO_START), 0);
        }

        SetForegroundWindow(window);
		global_dialog_window = window;

		return TRUE;
    }
    else if (message == WM_DESTROY)
    {
        global_dialog_window = 0;
        KillTimer(window, CAS_DIALOG_TIMER_HANDLE_ID);
    }
    else if (message == WM_COMMAND)
	{
        int control = LOWORD(wparam);

		if (control == ID_START)
        {
            if (!global_started)
            {
                CasDialogConfig* dialog_config = (CasDialogConfig*)GetWindowLongPtrW(window, GWLP_USERDATA);

                cas_dialog__config_save(window);

                if (cas_dialog_config_load(dialog_config))
                {
                    global_started = 1;

                    SetTimer(window, CAS_DIALOG_TIMER_HANDLE_ID, 1000, 0);

                    for (unsigned int i = 0; i < MAX_ITEMS; ++i)
                    {
                        EnableWindow(GetDlgItem(window, ID_PROCESS + i), 0);
                        EnableWindow(GetDlgItem(window, ID_AFFINITY_MASK + i), 0);
                    }
                    EnableWindow(GetDlgItem(window, ID_PERIOD), 0);

                    UINT period = GetPrivateProfileIntW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_PERIOD_KEY, 5, global_ini_path);
                    cas_set_timer(period);
                }

                return TRUE;
            }
        }
        else if (control == ID_STOP)
        {
            if (global_started)
            {
                CasDialogConfig* dialog_config = (CasDialogConfig*)GetWindowLongPtrW(window, GWLP_USERDATA);

                global_started = 0;

                KillTimer(window, CAS_DIALOG_TIMER_HANDLE_ID);

                for (unsigned int i = 0; i < MAX_ITEMS; ++i)
                {
                    EnableWindow(GetDlgItem(window, ID_PROCESS + i), 1);
                    EnableWindow(GetDlgItem(window, ID_AFFINITY_MASK + i), 1);
                    SetDlgItemTextW(window, ID_SET + i, L"");
                    dialog_config->dones[i] = 0;
                }
                EnableWindow(GetDlgItem(window, ID_PERIOD), 1);

                cas_stop_timer();

                return TRUE;
            }
        }
        else if (control == ID_CANCEL)
        {
            KillTimer(window, CAS_DIALOG_TIMER_HANDLE_ID);
            EndDialog(window, 0);
            return FALSE;
        }
        else if (control == ID_VALUE_TYPE && HIWORD(wparam) == CBN_SELCHANGE)
		{
			LRESULT index = SendDlgItemMessageW(window, ID_VALUE_TYPE, CB_GETCURSEL, 0, 0);
            global_value_type = (unsigned int)index;
            SetDlgItemTextW(window, ID_RESULT, L"");
            SetDlgItemTextW(window, ID_VALUE, L"");
			return TRUE;
		}
        else if (control == ID_VALUE)
        {
            cas_dialog__convert_value(window);
        }
        else if (control >= ID_AFFINITY_MASK && control < ID_AFFINITY_MASK + MAX_ITEMS)
        {
            WCHAR* wrong_hex = 0;
            WCHAR affinity_mask_string[64] = { 0 };
            int affinity_mask_string_length = 0;

            affinity_mask_string_length = GetDlgItemTextW(window, control, affinity_mask_string, ARRAY_COUNT(affinity_mask_string));

            if (affinity_mask_string_length > (int)global_system_info.dwNumberOfProcessors / 2)
            {
                affinity_mask_string[global_system_info.dwNumberOfProcessors / 2] = '\0';
                SetDlgItemTextW(window, control, affinity_mask_string);
                SendDlgItemMessageW(window, control, EM_SETSEL, global_system_info.dwNumberOfProcessors, global_system_info.dwNumberOfProcessors);
            }
            else if (!cas_dialog__validate_hex(affinity_mask_string, affinity_mask_string_length, &wrong_hex))
            {
                if (wrong_hex)
                {
                    *wrong_hex = '\0';
                }

                SetDlgItemTextW(window, control, affinity_mask_string);
                SendDlgItemMessageW(window, control, EM_SETSEL, global_system_info.dwNumberOfProcessors, global_system_info.dwNumberOfProcessors);
            }

        }
        else if (control == ID_SILENT_START)
        {
            UINT checked = IsDlgButtonChecked(window, ID_SILENT_START);
            WritePrivateProfileStringW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_SILENT_START_KEY, checked ? L"1" : L"0", global_ini_path);
        }
        else if (control == ID_AUTO_START)
        {
            UINT checked = IsDlgButtonChecked(window, ID_AUTO_START);

            if (checked)
            {
                if (S_OK != cas_create_admin_task())
                {
                    MessageBoxW(0, L"Failed to create auto task.", L"Warning!", MB_ICONWARNING);
                    SendDlgItemMessageW(window, ID_AUTO_START, BM_SETCHECK, BST_UNCHECKED, 0);
                    WritePrivateProfileStringW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_AUTO_START_KEY, L"0", global_ini_path);
                }
                else
                {
                    WritePrivateProfileStringW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_AUTO_START_KEY, L"1", global_ini_path);
                }
            }
            else
            {
                if (S_OK != cas_delete_admin_task())
                {
                    MessageBoxW(0, L"Failed to delete auto task.", L"Warning!", MB_ICONWARNING);
                    SendDlgItemMessageW(window, ID_AUTO_START, BM_SETCHECK, BST_CHECKED, 0);
                    WritePrivateProfileStringW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_AUTO_START_KEY, L"1", global_ini_path);
                }
                else
                {
                    WritePrivateProfileStringW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_AUTO_START_KEY, L"0", global_ini_path);
                }
            }
        }
        else if (control == ID_PERIOD)
        {
            WCHAR period_string[3] = { 0 };

            UINT period = GetDlgItemInt(window, ID_PERIOD, 0, FALSE);

            if (period > 99)
            {
                period = 99;
                SetDlgItemInt(window, ID_PERIOD, period, FALSE);
            }

            _snwprintf(period_string, ARRAY_COUNT(period_string), L"%u", period);
            WritePrivateProfileStringW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_PERIOD_KEY, period_string, global_ini_path);
        }

        return TRUE;
    }
    else if (message == WM_TIMER)
	{
        if (global_started)
        {
            CasDialogConfig* dialog_config = (CasDialogConfig*)GetWindowLongPtrW(window, GWLP_USERDATA);

            for (unsigned int i = 0; i < MAX_ITEMS; ++i)
            {
                if (*dialog_config->processes[i])
                {
                    if (dialog_config->dones[i])
                    {
                        SetDlgItemTextW(window, ID_SET + i, (WCHAR*)global_check_mark);
                    }
                    else
                    {
                        SetDlgItemTextW(window, ID_SET + i, L"");
                    }
                }
                else
                {
                    break;
                }
            }
        }
    }

    return FALSE;
}

static void* cas_dialog__align(BYTE* data, SIZE_T size)
{
	SIZE_T pointer = (SIZE_T)data;
	return data + ((pointer + size - 1) & ~(size - 1)) - pointer;
}

static BYTE* cas__do_dialog_item(BYTE* buffer, LPCSTR text, WORD id, WORD control, DWORD style, int x, int y, int w, int h)
{
	buffer = cas_dialog__align(buffer, sizeof(DWORD));

	*(DLGITEMTEMPLATE*)buffer = (DLGITEMTEMPLATE)
	{
		.style = style | WS_CHILD | WS_VISIBLE,
		.x = (short)x,
		.y = (short)y + (control == CONTROL_STATIC ? 2 : 0),
		.cx = (short)w,
		.cy = (short)h - (control == CONTROL_EDIT ? 2 : 0) - (control == CONTROL_STATIC ? 2 : 0),
		.id = id,
	};
	buffer += sizeof(DLGITEMTEMPLATE);

	// window class
	buffer = cas_dialog__align(buffer, sizeof(WORD));
	*(WORD*)buffer = 0xffff;
	buffer += sizeof(WORD);
	*(WORD*)buffer = control;
	buffer += sizeof(WORD);

	// item text
	buffer = cas_dialog__align(buffer, sizeof(WCHAR));
	DWORD item_chars = MultiByteToWideChar(CP_UTF8, 0, text, -1, (WCHAR*)buffer, 128);
	buffer += item_chars * sizeof(WCHAR);

	// create extras
	buffer = cas_dialog__align(buffer, sizeof(WORD));
	*(WORD*)buffer = 0;
	buffer += sizeof(WORD);

	return buffer;
}

static void cas__do_dialog_layout(const CasDialogLayout* dialog_layout, BYTE* buffer, SIZE_T buffer_size)
{
	BYTE* end = buffer + buffer_size;

	// header
	DLGTEMPLATE* dialog = (void*)buffer;
	buffer += sizeof(DLGTEMPLATE);

	// menu
	buffer = cas_dialog__align(buffer, sizeof(WCHAR));
	*(WCHAR*)buffer = 0;
	buffer += sizeof(WCHAR);

	// window class
	buffer = cas_dialog__align(buffer, sizeof(WCHAR));
	*(WCHAR*)buffer = 0;
	buffer += sizeof(WCHAR);

	// title
	buffer = cas_dialog__align(buffer, sizeof(WCHAR));
	DWORD title_chars = MultiByteToWideChar(CP_UTF8, 0, dialog_layout->title, -1, (WCHAR*)buffer, 128);
	buffer += title_chars * sizeof(WCHAR);

	// font size
	buffer = cas_dialog__align(buffer, sizeof(WORD));
	*(WORD*)buffer = dialog_layout->font_size;
	buffer += sizeof(WORD);

	// font name
	buffer = cas_dialog__align(buffer, sizeof(WCHAR));
	DWORD font_chars = MultiByteToWideChar(CP_UTF8, 0, dialog_layout->font, -1, (WCHAR*)buffer, 128);
	buffer += font_chars * sizeof(WCHAR);

	int item_count = 3;

	int button_x = PADDING + 2 * (COL_WIDTH + PADDING) + COL2_WIDTH + PADDING - 3 * (PADDING + BUTTON_WIDTH);
	int button_y = PADDING + ROW_HEIGHT + PADDING + ROW2_HEIGHT + PADDING;

	DLGITEMTEMPLATE* start_buffer = cas_dialog__align(buffer, sizeof(DWORD));
	buffer = cas__do_dialog_item(buffer, "Start", ID_START, CONTROL_BUTTON, WS_TABSTOP | BS_DEFPUSHBUTTON, button_x, button_y, BUTTON_WIDTH, ITEM_HEIGHT);
	button_x += BUTTON_WIDTH + PADDING;
    (void)start_buffer;

	DLGITEMTEMPLATE* stop_buffer = cas_dialog__align(buffer, sizeof(DWORD));
	buffer = cas__do_dialog_item(buffer, "Stop", ID_STOP, CONTROL_BUTTON, WS_TABSTOP | BS_PUSHBUTTON, button_x, button_y, BUTTON_WIDTH, ITEM_HEIGHT);
	button_x += BUTTON_WIDTH + PADDING;
    (void)stop_buffer;

	DLGITEMTEMPLATE* cancel_buffer = cas_dialog__align(buffer, sizeof(DWORD));
	buffer = cas__do_dialog_item(buffer, "Cancel", ID_CANCEL, CONTROL_BUTTON, WS_TABSTOP | BS_PUSHBUTTON, button_x, button_y, BUTTON_WIDTH, ITEM_HEIGHT);
	button_x += BUTTON_WIDTH + PADDING;
    (void)cancel_buffer;

	for (const CasDialogGroup* group = dialog_layout->groups; group->caption; group++)
	{
		int x = group->rect.left + PADDING;
		int y = group->rect.top + PADDING;
		int w = group->rect.width;
		int h = group->rect.height;

		buffer = cas__do_dialog_item(buffer, group->caption, (WORD)-1, CONTROL_BUTTON, BS_GROUPBOX, x, y, w, h);
		item_count++;

		x += PADDING;
		y += ITEM_HEIGHT - PADDING;
		w -= 2 * PADDING;

		for (unsigned int item_index = 0; item_index < ARRAY_COUNT(group->items); item_index++)
		{
            const CasDialogItem* item = group->items + item_index;
			int has_number = !!(item->item & ITEM_NUMBER);
            int has_string = !!(item->item & ITEM_STRING);
            int has_const_string = !!(item->item & ITEM_CONST_STRING);
            int has_label = !!(item->item & ITEM_LABEL);
            int has_combobox = !!(item->item & ITEM_COMBOBOX);
            int has_checkbox = !!(item->item & ITEM_CHECKBOX);
            int has_center = !!(item->item & ITEM_COMBOBOX);

			int item_x = x;
			int item_w = w;
			int item_id = item->id;

            if (has_checkbox)
			{
				buffer = cas__do_dialog_item(buffer, item->text, (WORD)item_id, (WORD)CONTROL_BUTTON, WS_TABSTOP | BS_AUTOCHECKBOX, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
				item_id++;
			}

            if (has_label)
            {
                buffer = cas__do_dialog_item(buffer, item->text, (WORD)-1, (WORD)CONTROL_STATIC, 0, item_x, y, item->width, ITEM_HEIGHT);
				item_count++;
                item_x += item->width + PADDING;
				item_w -= item->width + PADDING;
            }

            if (has_string)
            {
                DWORD style = WS_TABSTOP | WS_BORDER;

                if (has_label)
                {
                    style |= ES_RIGHT;
                }

                buffer = cas__do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_EDIT, style, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
            }

            if (has_const_string)
            {
                DWORD style = 0;

                if (has_label)
                {
                    style |= ES_RIGHT;
                }

                if (has_center)
                {
                    style |= ES_CENTER;
                }

                buffer = cas__do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_STATIC, style, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
            }

			if (has_number)
			{
				buffer = cas__do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_EDIT, WS_TABSTOP | WS_BORDER | ES_NUMBER, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
			}

            if (has_combobox)
			{
				buffer = cas__do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_COMBOBOX, WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
			}

			y += ITEM_HEIGHT;
		}
	}

	*dialog = (DLGTEMPLATE)
	{
		.style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
		.cdit = (WORD)item_count,
		.cx = PADDING + COL_WIDTH + PADDING + COL_WIDTH + PADDING + COL2_WIDTH + PADDING,
		.cy = PADDING + ROW_HEIGHT + PADDING + ROW2_HEIGHT + PADDING + ITEM_HEIGHT + PADDING,
	};

	ASSERT(buffer <= end);
}

int cas_dialog_config_load(CasDialogConfig* dialog_config)
{
    int result = TRUE;
    WIN32_FILE_ATTRIBUTE_DATA data;

	if (!GetFileAttributesExW(global_ini_path, GetFileExInfoStandard, &data))
	{
        MessageBoxW(0, L"cas.ini may be deleted.", L"Warning!", MB_ICONWARNING);
		// .ini file deleted?
		return FALSE;
	}

    WCHAR settings[(sizeof(dialog_config->processes) + sizeof(dialog_config->affinity_masks))] = { 0 };

    GetPrivateProfileSectionW(CAS_DIALOG_INI_PAIRS_SECTION,
                              settings, ARRAY_COUNT(settings),
                              global_ini_path);

    WCHAR* pointer = settings;
    int pointer_length = 0;
    int count = 0;

    while (*pointer != 0)
    {
        if (count == MAX_ITEMS)
        {
            MessageBoxW(0, L"More than 16 items is not supported.", L"Warning!", MB_ICONWARNING);
            result = FALSE;
            break;
        }

        pointer_length = lstrlenW(pointer);

        WCHAR* pair = pointer;
        StrTrimW(pair, L" \t");

        if (pair[0])
        {
            WCHAR* colon = wcsrchr(pair, L':');

            if (colon)
            {
                *colon = '\0';

                lstrcpynW(dialog_config->processes[count], pair, ARRAY_COUNT(dialog_config->processes[count]));
                long long affinity_mask = wcstoll(colon + 1, 0, 16);

                if (!affinity_mask || affinity_mask == LLONG_MAX || affinity_mask == LLONG_MIN || affinity_mask > (long long)global_system_info.dwActiveProcessorMask)
                {
                    MessageBoxW(0, L"Affinity mask has wrong format.", L"Warning!", MB_ICONWARNING);
                    result = FALSE;
                    break;
                }
                else
                {
                    dialog_config->affinity_masks[count] = (UINT)affinity_mask;
                    ++count;
                }

            }
            else
            {
                MessageBoxW(0, L"cas.ini may be corrupted.", L"Warning!", MB_ICONWARNING);
                result = FALSE;
                break;
            }
        }

        pointer += pointer_length + 1;
    }

    return result;
}

LRESULT cas_dialog_show(CasDialogConfig* dialog_config)
{
	if (global_dialog_window)
	{
		SetForegroundWindow(global_dialog_window);
		return FALSE;
	}

    char* title[64] = { 0 };
    snprintf((char*)title, sizeof(title),
             "cas%s", global_is_elavated ? "" : " (no administrator rights)");
    char* affinity_masks_caption[64] = { 0 };
    snprintf((char*)affinity_masks_caption, sizeof(affinity_masks_caption),
             "Affinity Masks (Hex) - Max: %X", (int)global_system_info.dwActiveProcessorMask);
    char* auto_start[64] = { 0 };
    snprintf((char*)auto_start, sizeof(auto_start),
             "Auto-start%s", global_is_elavated ? "" : " (run as administrator)");

	CasDialogLayout dialog_layout = (CasDialogLayout)
	{
		.title = (const char*)title,
		.font = "Segoe UI",
		.font_size = 9,
		.groups = (CasDialogGroup[])
		{
            {
				.caption = "Processes",
				.rect = { 0, 0, COL_WIDTH, ROW_HEIGHT },
			},
			{
				.caption = (const char*)affinity_masks_caption,
				.rect = { COL_WIDTH + PADDING, 0, COL_WIDTH, ROW_HEIGHT },
			},
            {
				.caption = "Done",
				.rect = { (COL_WIDTH + PADDING) * 2, 0, COL2_WIDTH, ROW_HEIGHT },
			},
            {
				.caption = "Settings",
				.rect = { 0, ROW_HEIGHT, COL_WIDTH, ROW2_HEIGHT },
                .items =
                {
                    { "Period (sec)", ID_PERIOD,     ITEM_NUMBER | ITEM_LABEL, 48 },
                    { "Silent-start", ID_SILENT_START, ITEM_CHECKBOX },
                    { (const char*)auto_start,   ID_AUTO_START, ITEM_CHECKBOX },
                    { NULL },
                },
			},
            {
				.caption = "Convert",
				.rect = { COL_WIDTH + PADDING, ROW_HEIGHT, (COL_WIDTH + PADDING) + COL2_WIDTH, ROW2_HEIGHT },
                .items =
                {
                    { "Value Type",  ID_VALUE_TYPE,  ITEM_COMBOBOX | ITEM_LABEL, 48 },
                    { "Value",       ID_VALUE,       ITEM_STRING | ITEM_LABEL, 48 },
                    { "Result",      ID_RESULT,      ITEM_CONST_STRING | ITEM_LABEL, 48 },
                    { NULL },
                },
			},
			{ NULL },
		},
	};

    for (unsigned int i = 0; i < MAX_ITEMS; ++i)
    {
        CasDialogItem* process_item = dialog_layout.groups[0].items + i;
        CasDialogItem* affinity_mask_item = dialog_layout.groups[1].items + i;
        CasDialogItem* done_item = dialog_layout.groups[2].items + i;

        *process_item = (CasDialogItem){ "", (WORD)(ID_PROCESS + i), ITEM_STRING, MAX_ITEMS_LENGTH };
        *affinity_mask_item = (CasDialogItem){ "", (WORD)(ID_AFFINITY_MASK + i), ITEM_STRING, MAX_ITEMS_LENGTH };
        *done_item = (CasDialogItem){ "", (WORD)(ID_SET + i), ITEM_CONST_STRING | ITEM_CENTER };
    }

	BYTE __declspec(align(4)) buffer[4096];
	cas__do_dialog_layout(&dialog_layout, buffer, sizeof(buffer));

	return DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)buffer, NULL, cas_dialog__proc, (LPARAM)dialog_config);
}

void cas_dialog_init(CasDialogConfig* dialog_config, WCHAR* ini_path, HICON icon)
{
    UINT silent_start = GetPrivateProfileIntW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_SILENT_START_KEY, 0, ini_path);

    global_ini_path = ini_path;
    global_icon = icon;
    global_is_elavated = cas__is_elavated();

    GetSystemInfo(&global_system_info);

    if (silent_start)
    {
        UINT period = GetPrivateProfileIntW(CAS_DIALOG_INI_SETTINGS_SECTION, CAS_DIALOG_INI_PERIOD_KEY, 5, ini_path);

        global_started = 1;
        cas_dialog_config_load(dialog_config);
        cas_set_timer(period);
    }
    else
    {
        cas_dialog_config_load(dialog_config);
        cas_dialog_show(dialog_config);
    }
}
