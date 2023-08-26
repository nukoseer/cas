#ifndef H_CASCHEDULER_DIALOG_H

#define MAX_ITEMS 16
#define MAX_ITEMS_LENGTH 64

typedef struct
{
    WCHAR processes[MAX_ITEMS][64];
	WCHAR affinity_masks[MAX_ITEMS][64];
    WCHAR bit_mask[32];
    WCHAR hex_value[32];
} CASchedulerDialogConfig;

void cascheduler_dialog_config_load(CASchedulerDialogConfig* dialog_config);
BOOL cascheduler_dialog_show(CASchedulerDialogConfig* dialog_config);

#define H_CASCHEDULER_DIALOG_H
#endif
