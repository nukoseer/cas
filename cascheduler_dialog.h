#ifndef H_CASCHEDULER_DIALOG_H

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
} CASchedulerDialogConfig;

typedef void CASchedulerDialogCallback(void* parameter);

#define CASCHEDULER_DIALOG_CALLBACK(name) void (name)(void* parameter)

void cascheduler_dialog_register_callback(CASchedulerDialogCallback* dialog_callback, void* parameter, unsigned int id);
void cascheduler_dialog_config_load(CASchedulerDialogConfig* dialog_config);
LRESULT cascheduler_dialog_show(CASchedulerDialogConfig* dialog_config);
void cascheduler_dialog_init(WCHAR* ini_path);

#define H_CASCHEDULER_DIALOG_H
#endif
