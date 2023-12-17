#ifndef H_CAS_DIALOG_H

#define MAX_ITEMS 16
#define MAX_ITEMS_LENGTH 64

#define ID_START   0
#define ID_STOP    1
#define ID_CANCEL  2

#define HOT_KEY(key, mod) ((key) | ((mod) << 24))
#define HOT_GET_KEY(key_mod) ((key_mod) & 0xffffff)
#define HOT_GET_MOD(key_mod) (((key_mod) >> 24) & 0xff)

typedef struct
{
    WCHAR processes[MAX_ITEMS][64];
	UINT affinity_masks[MAX_ITEMS];
    BOOL dones[MAX_ITEMS];
    DWORD value_type;
    DWORD menu_shortcut;
} CasDialogConfig;

int cas_dialog_config_load(CasDialogConfig* dialog_config);
LRESULT cas_dialog_show(CasDialogConfig* dialog_config);
void cas_dialog_init(CasDialogConfig* dialog_config, WCHAR* ini_path, HICON icon);

#define H_CAS_DIALOG_H
#endif
