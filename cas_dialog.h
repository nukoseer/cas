#ifndef H_CAS_DIALOG_H

#define MAX_ITEMS 16
#define MAX_ITEMS_LENGTH 64

#define ID_START   0
#define ID_STOP    1
#define ID_CANCEL  2

typedef struct
{
    WCHAR processes[MAX_ITEMS][64];
	UINT affinity_masks[MAX_ITEMS];
    BOOL sets[MAX_ITEMS];
    DWORD value_type;
} CasDialogConfig;

typedef void CasDialogCallback(void* parameter);

#define CAS_DIALOG_CALLBACK(name) void (name)(void* parameter)

void cas_dialog_register_callback(CasDialogCallback* dialog_callback, void* parameter, unsigned int id);
void cas_dialog_config_load(CasDialogConfig* dialog_config);
LRESULT cas_dialog_show(CasDialogConfig* dialog_config);
void cas_dialog_init(CasDialogConfig* dialog_config, WCHAR* ini_path);

#define H_CAS_DIALOG_H
#endif
