// NOTE: Source: https://github.com/mmozeiko/wcap/blob/main/wcap_config.c

#include "cascheduler.h"
#include "cascheduler_dialog.h"

#define COL_WIDTH  150
#define ROW_HEIGHT ((MAX_ITEMS + 1) * ITEM_HEIGHT)
#define ROW2_HEIGHT ((2 + 1) * ITEM_HEIGHT)

#define BUTTON_WIDTH 50
#define ITEM_HEIGHT  14

#define ID_START 1
#define ID_STOP  2
#define ID_QUIT  3
#define ID_PROCESS 100
#define ID_AFFINITY_MASK 200
#define ID_BIT_MASK 300
#define ID_HEX_VALUE 301

#define ITEM_NUMBER       (1 << 1)
#define ITEM_STRING       (1 << 2)
#define ITEM_CONST_STRING (1 << 3)
#define ITEM_LABEL        (1 << 4)

#define PADDING 4

// win32 control styles
#define CONTROL_BUTTON    0x0080
#define CONTROL_EDIT      0x0081
#define CONTROL_STATIC    0x0082
// #define CONTROL_LISTBOX   0x0083
// #define CONTROL_SCROLLBAR 0x0084
// #define CONTROL_COMBOBOX  0x0085

typedef struct
{
	int left;
	int top;
	int width;
	int height;
} CASchedulerDialogRect;

typedef struct
{
	const char* text;
	WORD id;
	WORD item;
	DWORD width;
} CASchedulerDialogItem;

typedef struct
{
	const char* caption;
	CASchedulerDialogRect rect;
	CASchedulerDialogItem items[MAX_ITEMS];
} CASchedulerDialogGroup;

typedef struct
{
	CASchedulerDialogGroup* groups;
    const char* title;
	const char* font;
    WORD font_size;
} CASchedulerDialogLayout;

static void* cascheduler_dialog_align(BYTE* data, SIZE_T size)
{
	SIZE_T pointer = (SIZE_T)data;
	return data + ((pointer + size - 1) & ~(size - 1)) - pointer;
}

static HWND global_dialog_window;
static int global_started;

// typedef struct
// {
//     WCHAR* processes[MAX_ITEMS];
// 	WCHAR* affinity_masks[MAX_ITEMS];
// } CASchedulerProcessAffinity;

// static CASchedulerProcessAffinity process_affinitities;

#define INI_PATH (L"C:\\Programming\\set-cpu-affinity\\build\\CPUAffinityScheduler.ini")

static void cascheduler_dialog_set_values(HWND window, CASchedulerDialogConfig* dialog_config)
{
    for (unsigned int i = 0; i < MAX_ITEMS; ++i)
    {
        SetDlgItemTextW(window, ID_PROCESS + i, dialog_config->processes[i]);
        SetDlgItemTextW(window, ID_AFFINITY_MASK + i, dialog_config->affinity_masks[i]);
    }
}

void cascheduler_dialog_config_load(CASchedulerDialogConfig* dialog_config)
{
    WIN32_FILE_ATTRIBUTE_DATA data;

	if (!GetFileAttributesExW(INI_PATH, GetFileExInfoStandard, &data))
	{
		// .ini file deleted?
		return;
	}

    WCHAR settings[(64 + 32 + 1) * 16] = { 0 };

    GetPrivateProfileSectionW(L"settings",
                              settings, ARRAY_COUNT(settings),
                              INI_PATH);
    
    WCHAR* pointer = settings;
    int pointer_length = 0;
    int count = 0;
                
    while (*pointer != 0)
    {
        if (count == MAX_ITEMS)
        {
            MessageBoxW(0, L"More than 16 items is not supported.", L"Warning!", MB_ICONWARNING);
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
                lstrcpynW(dialog_config->affinity_masks[count], colon + 1, ARRAY_COUNT(dialog_config->processes[count]));
                ++count;
            }
        }

        pointer += pointer_length + 1;
    }
}

static int validate_bits(const WCHAR* bits, int length, const WCHAR** wrong_bit)
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

static unsigned int bits_to_integer(const WCHAR* bits, int length)
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

static LRESULT CALLBACK cascheduler_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)wparam;
    
    if (message == WM_INITDIALOG)
	{
        CASchedulerDialogConfig* dialog_config = (CASchedulerDialogConfig*)lparam;

		SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)dialog_config);

        cascheduler_dialog_set_values(window, dialog_config);

        // SetDlgItemInt(window, 100, 0, FALSE);
        
        // SendDlgItemMessageW(window, 100, WM_CTLCOLORSTATIC, RGB(0, 144, 0), 0);
        // EnableWindow(GetDlgItem(window, 100), 0);
        
		// SendDlgItemMessageW(Window, ID_VIDEO_CODEC, CB_ADDSTRING, 0, (LPARAM)L"H264 / AVC");
		// SendDlgItemMessageW(Window, ID_VIDEO_CODEC, CB_ADDSTRING, 0, (LPARAM)L"H265 / HEVC");

		// SendDlgItemMessageW(Window, ID_AUDIO_CODEC, CB_ADDSTRING, 0, (LPARAM)L"AAC");
		// SendDlgItemMessageW(Window, ID_AUDIO_CODEC, CB_ADDSTRING, 0, (LPARAM)L"FLAC");

		// SendDlgItemMessageW(Window, ID_AUDIO_CHANNELS, CB_ADDSTRING, 0, (LPARAM)L"1");
		// SendDlgItemMessageW(Window, ID_AUDIO_CHANNELS, CB_ADDSTRING, 0, (LPARAM)L"2");

		// SendDlgItemMessageW(Window, ID_AUDIO_SAMPLERATE, CB_ADDSTRING, 0, (LPARAM)L"44100");
		// SendDlgItemMessageW(Window, ID_AUDIO_SAMPLERATE, CB_ADDSTRING, 0, (LPARAM)L"48000");

		// SendDlgItemMessageW(Window, ID_GPU_ENCODER + 1, CB_ADDSTRING, 0, (LPARAM)L"Prefer iGPU");
		// SendDlgItemMessageW(Window, ID_GPU_ENCODER + 1, CB_ADDSTRING, 0, (LPARAM)L"Prefer dGPU");

		// Config__SetDialogValues(Window, Config);

		SetForegroundWindow(window);
		global_dialog_window = window;

		return TRUE;
    }
    else if (message == WM_COMMAND)
	{
        int control = LOWORD(wparam);

		if (control == ID_START)
        {
            // CASchedulerDialogConfig* dialog_config = (CASchedulerDialogConfig*)GetWindowLongPtrW(window, GWLP_USERDATA);
           if (!global_started)
            {
                global_started = 1;

                for (unsigned int i = 0; i < MAX_ITEMS; ++i)
                {
                    WCHAR process_string[64] = { 0 };
                    WCHAR affinity_mask_string[64] = { 0 };

                    GetDlgItemTextW(window, ID_PROCESS + i, process_string, ARRAY_COUNT(process_string));
                    GetDlgItemTextW(window, ID_AFFINITY_MASK + i, affinity_mask_string, ARRAY_COUNT(affinity_mask_string));
                    EnableWindow(GetDlgItem(window, ID_PROCESS + i), 0);
                    EnableWindow(GetDlgItem(window, ID_AFFINITY_MASK + i), 0);
                }   
            }

           return TRUE;
        }
        else if (control == ID_STOP)
        {
            if (global_started)
            {
                global_started = 0;
            
                for (unsigned int i = 0; i < MAX_ITEMS; ++i)
                {
                    EnableWindow(GetDlgItem(window, ID_PROCESS + i), 1);
                    EnableWindow(GetDlgItem(window, ID_AFFINITY_MASK + i), 1);
                }   
            }

            return TRUE;
        }
        else if (control == ID_QUIT)
        {
            // TODO: Probably we should not do it here? Or not like this.
            EndDialog(window, 0);
            ExitProcess(0);
        }
        else if (control == ID_BIT_MASK)
        {
            WCHAR bit_mask_string[64] = { 0 };
            int bit_mask_length = 0;
            WCHAR hex_value_string[32] = { 0 };
            
            bit_mask_length = GetDlgItemTextW(window, ID_BIT_MASK, bit_mask_string, ARRAY_COUNT(bit_mask_string));

            if (bit_mask_length > 0)
            {
                WCHAR* wrong_bit = 0;

                if (bit_mask_length > 32)
                {
                    bit_mask_string[32] = '\0';
                    SetDlgItemTextW(window, ID_BIT_MASK, bit_mask_string);
                    SendDlgItemMessageW(window, ID_BIT_MASK, EM_SETSEL, 32, 32);
                }
                else if (validate_bits(bit_mask_string, bit_mask_length, &wrong_bit))
                {
                    unsigned int result = bits_to_integer(bit_mask_string, bit_mask_length);
                
                    _snwprintf(hex_value_string, ARRAY_COUNT(hex_value_string), L"%u", result);
                    SetDlgItemTextW(window, ID_HEX_VALUE, hex_value_string);
                }
                else
                {
                    if (wrong_bit)
                    {
                        *wrong_bit = '\0';   
                    }

                    SetDlgItemTextW(window, ID_BIT_MASK, bit_mask_string);
                    SendDlgItemMessageW(window, ID_BIT_MASK, EM_SETSEL, (WPARAM)bit_mask_length - 1, (LPARAM)bit_mask_length - 1);
                }
            }
        }

        return TRUE;
    }
    // else if (message == WM_CTLCOLOREDIT)
    // {
    //     SetBkMode((HDC)wparam, TRANSPARENT);
    //     // SetBkColor((HDC)wparam, RGB(0, 144, 0));
    //     SetTextColor((HDC)wparam, RGB(0, 128, 0));

    //     return GetSysColor(COLOR_MENU);
    // }
    // else if (message == WM_CTLCOLORSTATIC)
    // {
    //     // if (global_started)
    //     {
    //         // if (GetDlgItem(window, 100) == (HWND)lparam)
    //         {
    //             // SetBkMode((HDC)wparam, TRANSPARENT);
    //             // SetTextColor((HDC)wparam,  RGB(0, 128, 0));
    //             SetBkColor((HDC)wparam, RGB(0, 128, 0));
    //         }   
    //     }
        
    //     return GetSysColor(COLOR_MENU);
    // }

    return FALSE;
}

static BYTE* cascheduler_do_dialog_item(BYTE* buffer, LPCSTR text, WORD id, WORD control, DWORD style, int x, int y, int w, int h)
{
	buffer = cascheduler_dialog_align(buffer, sizeof(DWORD));

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
	buffer = cascheduler_dialog_align(buffer, sizeof(WORD));
	*(WORD*)buffer = 0xffff;
	buffer += sizeof(WORD);
	*(WORD*)buffer = control;
	buffer += sizeof(WORD);

	// item text
	buffer = cascheduler_dialog_align(buffer, sizeof(WCHAR));
	DWORD item_chars = MultiByteToWideChar(CP_UTF8, 0, text, -1, (WCHAR*)buffer, 128);
	buffer += item_chars * sizeof(WCHAR);

	// create extras
	buffer = cascheduler_dialog_align(buffer, sizeof(WORD));
	*(WORD*)buffer = 0;
	buffer += sizeof(WORD);

	return buffer;
}

static void cascheduler_do_dialog_layout(const CASchedulerDialogLayout* dialog_layout, BYTE* buffer, SIZE_T buffer_size)
{
	BYTE* end = buffer + buffer_size;

	// header
	DLGTEMPLATE* dialog = (void*)buffer;
	buffer += sizeof(DLGTEMPLATE);

	// menu
	buffer = cascheduler_dialog_align(buffer, sizeof(WCHAR));
	*(WCHAR*)buffer = 0;
	buffer += sizeof(WCHAR);

	// window class
	buffer = cascheduler_dialog_align(buffer, sizeof(WCHAR));
	*(WCHAR*)buffer = 0;
	buffer += sizeof(WCHAR);

	// title
	buffer = cascheduler_dialog_align(buffer, sizeof(WCHAR));
	DWORD title_chars = MultiByteToWideChar(CP_UTF8, 0, dialog_layout->title, -1, (WCHAR*)buffer, 128);
	buffer += title_chars * sizeof(WCHAR);

	// font size
	buffer = cascheduler_dialog_align(buffer, sizeof(WORD));
	*(WORD*)buffer = dialog_layout->font_size;
	buffer += sizeof(WORD);

	// font name
	buffer = cascheduler_dialog_align(buffer, sizeof(WCHAR));
	DWORD font_chars = MultiByteToWideChar(CP_UTF8, 0, dialog_layout->font, -1, (WCHAR*)buffer, 128);
	buffer += font_chars * sizeof(WCHAR);

	int item_count = 3;

	int button_x = PADDING + 2 * COL_WIDTH + PADDING - 3 * (PADDING + BUTTON_WIDTH);
	int button_y = PADDING + ROW_HEIGHT + PADDING + ROW2_HEIGHT + PADDING;

	DLGITEMTEMPLATE* start_buffer = cascheduler_dialog_align(buffer, sizeof(DWORD));
	buffer = cascheduler_do_dialog_item(buffer, "Start", ID_START, CONTROL_BUTTON, WS_TABSTOP | BS_DEFPUSHBUTTON, button_x, button_y, BUTTON_WIDTH, ITEM_HEIGHT);
	button_x += BUTTON_WIDTH + PADDING;
    (void)start_buffer;

	DLGITEMTEMPLATE* stop_buffer = cascheduler_dialog_align(buffer, sizeof(DWORD));
	buffer = cascheduler_do_dialog_item(buffer, "Stop", ID_STOP, CONTROL_BUTTON, WS_TABSTOP | BS_PUSHBUTTON, button_x, button_y, BUTTON_WIDTH, ITEM_HEIGHT);
	button_x += BUTTON_WIDTH + PADDING;
    (void)stop_buffer;

	DLGITEMTEMPLATE* quit_buffer = cascheduler_dialog_align(buffer, sizeof(DWORD));
	buffer = cascheduler_do_dialog_item(buffer, "Quit", ID_QUIT, CONTROL_BUTTON, WS_TABSTOP | BS_PUSHBUTTON, button_x, button_y, BUTTON_WIDTH, ITEM_HEIGHT);
	button_x += BUTTON_WIDTH + PADDING;
    (void)quit_buffer;

	for (const CASchedulerDialogGroup* group = dialog_layout->groups; group->caption; group++)
	{
		int x = group->rect.left + PADDING;
		int y = group->rect.top + PADDING;
		int w = group->rect.width;
		int h = group->rect.height;

		buffer = cascheduler_do_dialog_item(buffer, group->caption, (WORD)-1, CONTROL_BUTTON, BS_GROUPBOX, x, y, w, h);
		item_count++;

		x += PADDING;
		y += ITEM_HEIGHT - PADDING;
		w -= 2 * PADDING;

		for (unsigned int item_index = 0; item_index < ARRAY_COUNT(group->items); item_index++)
		{
            const CASchedulerDialogItem* item = group->items + item_index;
			int has_number = !!(item->item & ITEM_NUMBER);
            int has_string = !!(item->item & ITEM_STRING);
            int has_const_string = !!(item->item & ITEM_CONST_STRING);
            int has_label =  !!(item->item & ITEM_LABEL);

			int item_x = x;
			int item_w = w;
			int item_id = item->id;

            if (has_label)
            {
                buffer = cascheduler_do_dialog_item(buffer, item->text, (WORD)-1, (WORD)CONTROL_STATIC, 0, item_x, y, item->width, ITEM_HEIGHT);
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
                
                buffer = cascheduler_do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_EDIT, style, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
            }

            if (has_const_string)
            {
                DWORD style = 0;

                if (has_label)
                {
                    style |= ES_RIGHT;
                }
                
                buffer = cascheduler_do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_STATIC, style, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
            }

			if (has_number)
			{
				buffer = cascheduler_do_dialog_item(buffer, "", (WORD)item_id, (WORD)CONTROL_EDIT, WS_TABSTOP | WS_BORDER | ES_NUMBER, item_x, y, item_w, ITEM_HEIGHT);
				item_count++;
			}

 

			y += ITEM_HEIGHT;
		}
	}

	*dialog = (DLGTEMPLATE)
	{
		.style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
		.cdit = (WORD)item_count,
		.cx = PADDING + COL_WIDTH + PADDING + COL_WIDTH + PADDING,
		.cy = PADDING + ROW_HEIGHT + PADDING + ROW2_HEIGHT + PADDING + ITEM_HEIGHT + PADDING,
	};

	ASSERT(buffer <= end);
}

BOOL cascheduler_dialog_show(CASchedulerDialogConfig* dialog_config)
{
	if (global_dialog_window)
	{
		SetForegroundWindow(global_dialog_window);
		return FALSE;
	}

	CASchedulerDialogLayout dialog_layout = (CASchedulerDialogLayout)
	{
		.title = "CPU Affinity Scheduler",
		.font = "Segoe UI",
		.font_size = 9,
		.groups = (CASchedulerDialogGroup[])
		{
			{
				.caption = "Processes",
				.rect = { 0, 0, COL_WIDTH, ROW_HEIGHT },
			},
			{
				.caption = "Affinity Masks (Hex)",
				.rect = { COL_WIDTH + PADDING, 0, COL_WIDTH, ROW_HEIGHT },
			},
            {
				.caption = "Calculate",
				.rect = { 0, ROW_HEIGHT, COL_WIDTH * 2, ROW2_HEIGHT },
                .items =
                {
                    { "Bit Mask",  ID_BIT_MASK,  ITEM_STRING | ITEM_LABEL, 32 },
                    { "Hex Value", ID_HEX_VALUE, ITEM_CONST_STRING | ITEM_LABEL, 32 },
                    { NULL },
                },
			},
			{ NULL },
		},
	};

    for (unsigned int i = 0; i < MAX_ITEMS; ++i)
    {
        CASchedulerDialogItem* process_item = dialog_layout.groups[0].items + i;
        CASchedulerDialogItem* affinity_mask_item = dialog_layout.groups[1].items + i;

        *process_item = (CASchedulerDialogItem){ "", (WORD)(ID_PROCESS + i), ITEM_STRING, MAX_ITEMS_LENGTH };
        *affinity_mask_item = (CASchedulerDialogItem){ "", (WORD)(ID_AFFINITY_MASK + i), ITEM_NUMBER, MAX_ITEMS_LENGTH };
    }

	BYTE __declspec(align(4)) buffer[4096];
	cascheduler_do_dialog_layout(&dialog_layout, buffer, sizeof(buffer));

	return (BOOL)DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)buffer, NULL, cascheduler_dialog_proc, (LPARAM)dialog_config);
}
