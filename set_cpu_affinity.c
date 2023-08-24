#pragma warning(push, 0)
#include <Windows.h>
#include <tlhelp32.h>
#pragma warning(pop)

#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "Kernel32")
#pragma comment(lib, "Advapi32")

static const char* process_name;
static unsigned int desired_affinity_mask;

static void print_bits(const char* label, unsigned int bits)
{
    unsigned int i = 0;
    unsigned int size = sizeof(bits) * 8;
    unsigned int mask = 0x80000000;

    if (label)
        fprintf(stderr, "%s ", label);

    for (i = 0; i < size; ++i)
    {
        fprintf(stderr, "%d", !!(bits & mask));
        mask >>= 1;
    }

    fprintf(stderr, "\n");
}

static void print_affinity_mask(const char* label, unsigned int affinity_mask)
{
    fprintf(stderr, "  %s:\n", label);
    fprintf(stderr, "    Hex   : 0x%X\n", (unsigned int)affinity_mask);
    print_bits("    Binary:", affinity_mask);
}

static int parse_args(int argc, const char* argv[])
{
    int result = 0;

    if (argc == 3)
    {
        const char* str_affinity_mask = argv[2];

        process_name = argv[1];

        if (str_affinity_mask[0] == '0' && (str_affinity_mask[1] == 'x' || str_affinity_mask[1] == 'X'))
        {
            desired_affinity_mask = strtol(str_affinity_mask, 0, 0);
        }
        else
        {
            desired_affinity_mask = strtol(str_affinity_mask, 0, 10);
        }
    }

    if (process_name && desired_affinity_mask)
    {
        result = 1;
    }

    return result;
}

static void enable_debug_privilege(void)
{
    HANDLE handle_token = 0;
    LUID luid = { 0 };
    TOKEN_PRIVILEGES tkp = { 0 };

    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &handle_token);

    LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid);

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(handle_token, 0, &tkp, sizeof(tkp), NULL, NULL);

    CloseHandle(handle_token);
}

int main(int argc, const char* argv[])
{
    if (parse_args(argc, argv))
    {
        int found = 0;
        PROCESSENTRY32 entry = { .dwSize = sizeof(PROCESSENTRY32) };
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        enable_debug_privilege();

        if (Process32First(snapshot, &entry))
        {
            while (Process32Next(snapshot, &entry))
            {
                if (!strcmp(entry.szExeFile, process_name))
                {
                    HANDLE handle_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE, entry.th32ProcessID);
                    DWORD process_affinity_mask = 0;
                    DWORD system_affinity_mask = 0;

                    found = 1;

                    if (handle_process)
                    {
                        fprintf(stderr, "Setting affinity mask 0x%X for %s:\n", desired_affinity_mask, entry.szExeFile);

                        if (GetProcessAffinityMask(handle_process, (PDWORD_PTR)&process_affinity_mask, (PDWORD_PTR)&system_affinity_mask))
                        {
                            print_affinity_mask("Initial affinity mask", process_affinity_mask);
                        }

                        if (SetProcessAffinityMask(handle_process, desired_affinity_mask))
                        {
                            print_affinity_mask("New affinity mask", desired_affinity_mask);
                            fprintf(stderr, "SUCCESSFUL!\n");
                        }
                        else
                        {
                            fprintf(stderr, "  Could not set!\n");
                        }

                        fprintf(stderr, "\n");
                    }
                    else
                    {
                        fprintf(stderr, "'%s' could not open. Please try to run set_cpu_affinity as administrator.\n", process_name);
                        fprintf(stderr, "FAILED!\n");
                    }

                    CloseHandle(handle_process);
                }
            }

            if (!found)
            {
                fprintf(stderr, "'%s' could not find.\n", process_name);
                fprintf(stderr, "FAILED!\n");
            }
        }

        CloseHandle(snapshot);
    }
    else
    {
        fprintf(stderr, "Usage: set_cpu_affinity <process_name> <affinity_mask (0xHEX or INTEGER)>");
    }

    {
        unsigned int c = getchar();
        (void)c;
    }

    return 0;
}
