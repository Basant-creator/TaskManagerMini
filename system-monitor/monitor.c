#define _WIN32_WINNT 0x0600
#define WINVER 0x0600

#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")

#define MAX_TRACKED_PIDS 2048

typedef struct {
    DWORD pid;
    char name[256];
    double usage;
} Process;

typedef struct {
    DWORD pid;
    ULARGE_INTEGER prevTime;
} ProcessTracker;

ProcessTracker tracked_processes[MAX_TRACKED_PIDS];
int tracked_process_count = 0;

ULARGE_INTEGER prev_sys_idle, prev_sys_kernel, prev_sys_user;
int num_cores = 1;

void init_system_info() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    num_cores = sysInfo.dwNumberOfProcessors;
    if (num_cores < 1) num_cores = 1;

    FILETIME ftime, fsys, fuser;
    GetSystemTimes(&ftime, &fsys, &fuser);
    prev_sys_idle.LowPart = ftime.dwLowDateTime;
    prev_sys_idle.HighPart = ftime.dwHighDateTime;
    prev_sys_kernel.LowPart = fsys.dwLowDateTime;
    prev_sys_kernel.HighPart = fsys.dwHighDateTime;
    prev_sys_user.LowPart = fuser.dwLowDateTime;
    prev_sys_user.HighPart = fuser.dwHighDateTime;
}

double get_cpu_usage(ULARGE_INTEGER *sys_diff_out) {
    FILETIME ftime, fsys, fuser;
    if (!GetSystemTimes(&ftime, &fsys, &fuser)) return 0.0;

    ULARGE_INTEGER current_idle, current_kernel, current_user;
    current_idle.LowPart = ftime.dwLowDateTime;
    current_idle.HighPart = ftime.dwHighDateTime;
    current_kernel.LowPart = fsys.dwLowDateTime;
    current_kernel.HighPart = fsys.dwHighDateTime;
    current_user.LowPart = fuser.dwLowDateTime;
    current_user.HighPart = fuser.dwHighDateTime;

    ULONGLONG idle_diff = current_idle.QuadPart - prev_sys_idle.QuadPart;
    ULONGLONG kernel_diff = current_kernel.QuadPart - prev_sys_kernel.QuadPart;
    ULONGLONG user_diff = current_user.QuadPart - prev_sys_user.QuadPart;

    ULONGLONG total_diff = kernel_diff + user_diff;

    prev_sys_idle = current_idle;
    prev_sys_kernel = current_kernel;
    prev_sys_user = current_user;

    sys_diff_out->QuadPart = total_diff;

    if (total_diff == 0) return 0.0;
    
    double usage = (double)(total_diff - idle_diff) * 100.0 / (double)total_diff;
    if (usage < 0.0) usage = 0.0;
    return usage;
}

void print_memory_usage() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        double gb_total = memInfo.ullTotalPhys / 1073741824.0;
        double gb_avail = memInfo.ullAvailPhys / 1073741824.0;
        double gb_used = gb_total - gb_avail;
        printf("Memory Usage: %.2f GB / %.2f GB (%.1f%%)\n", gb_used, gb_total, (double)memInfo.dwMemoryLoad);
    } else {
        printf("Memory Usage: N/A\n");
    }
}

void print_disk_usage() {
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExA("C:\\", &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        ULONGLONG used = totalNumberOfBytes.QuadPart - totalNumberOfFreeBytes.QuadPart;
        double pct = ((double)used / (double)totalNumberOfBytes.QuadPart) * 100.0;
        printf("Disk Usage (C:\\): %.1f%%\n", pct);
    } else {
        printf("Disk Usage (C:\\): N/A\n");
    }
}

ULONGLONG prev_net_sent = 0;
ULONGLONG prev_net_recv = 0;
DWORD prev_net_time = 0;

void print_network_usage() {
    ULONG dwSize = 0;
    if (GetIfTable(NULL, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER) {
        MIB_IFTABLE *pIfTable = (MIB_IFTABLE *)malloc(dwSize);
        if (pIfTable != NULL) {
            if (GetIfTable(pIfTable, &dwSize, 0) == NO_ERROR) {
                ULONGLONG rx_bytes = 0;
                ULONGLONG tx_bytes = 0;
                for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
                    MIB_IFROW *row = &pIfTable->table[i];
                    if (row->dwType != IF_TYPE_SOFTWARE_LOOPBACK) {
                        rx_bytes += row->dwInOctets;
                        tx_bytes += row->dwOutOctets;
                    }
                }
                
                DWORD current_time = GetTickCount();
                if (prev_net_time != 0) {
                    DWORD time_diff = current_time - prev_net_time;
                    if (time_diff > 0) {
                        double rx_mbps = (double)(rx_bytes - prev_net_recv) * 8.0 / 1000000.0 / (time_diff / 1000.0);
                        double tx_mbps = (double)(tx_bytes - prev_net_sent) * 8.0 / 1000000.0 / (time_diff / 1000.0);
                        if (rx_mbps < 0) rx_mbps = 0;
                        if (tx_mbps < 0) tx_mbps = 0;
                        printf("Network Speed: Upload: %.2f Mbps | Download: %.2f Mbps\n", tx_mbps, rx_mbps);
                    }
                } else {
                    printf("Network Speed: Calculating...\n");
                }
                prev_net_recv = rx_bytes;
                prev_net_sent = tx_bytes;
                prev_net_time = current_time;
            } else {
                printf("Network: Error reading stats\n");
            }
            free(pIfTable);
        }
    } else {
        printf("Network: Error determining size\n");
    }
}

void print_top_processes(ULARGE_INTEGER sys_diff) {
    DWORD aProcesses[2048], cbNeeded, cProcesses;
    if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) return;
    
    cProcesses = cbNeeded / sizeof(DWORD);
    Process top5[5] = {0};
    
    ProcessTracker new_tracked[MAX_TRACKED_PIDS];
    int new_tracked_count = 0;

    for (DWORD i = 0; i < cProcesses; i++) {
        if (aProcesses[i] == 0) continue;
        DWORD pid = aProcesses[i];
        
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess) {
            FILETIME ftime, fexit, fsys, fuser;
            if (GetProcessTimes(hProcess, &ftime, &fexit, &fsys, &fuser)) {
                ULARGE_INTEGER current_sys, current_user;
                current_sys.LowPart = fsys.dwLowDateTime;
                current_sys.HighPart = fsys.dwHighDateTime;
                current_user.LowPart = fuser.dwLowDateTime;
                current_user.HighPart = fuser.dwHighDateTime;
                
                ULARGE_INTEGER proc_ticks;
                proc_ticks.QuadPart = current_sys.QuadPart + current_user.QuadPart;
                
                ULARGE_INTEGER prev_proc_ticks;
                prev_proc_ticks.QuadPart = 0;
                for (int j = 0; j < tracked_process_count; j++) {
                    if (tracked_processes[j].pid == pid) {
                        prev_proc_ticks = tracked_processes[j].prevTime;
                        break;
                    }
                }
                
                double usage = 0.0;
                if (prev_proc_ticks.QuadPart != 0 && sys_diff.QuadPart > 0) {
                    ULONGLONG proc_diff = 0;
                    if (proc_ticks.QuadPart >= prev_proc_ticks.QuadPart) {
                        proc_diff = proc_ticks.QuadPart - prev_proc_ticks.QuadPart;
                    }
                    usage = 100.0 * num_cores * (double)proc_diff / (double)sys_diff.QuadPart;
                }
                
                if (new_tracked_count < MAX_TRACKED_PIDS) {
                    new_tracked[new_tracked_count].pid = pid;
                    new_tracked[new_tracked_count].prevTime = proc_ticks;
                    new_tracked_count++;
                }

                char szProcessName[MAX_PATH] = "<unknown>";
                if (GetProcessImageFileNameA(hProcess, szProcessName, sizeof(szProcessName))) {
                    char *basename = strrchr(szProcessName, '\\');
                    if (basename) {
                        memmove(szProcessName, basename + 1, strlen(basename + 1) + 1);
                    }
                } else {
                    HMODULE hMod;
                    DWORD cbNeededMod;
                    if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeededMod)) {
                        GetModuleBaseNameA(hProcess, hMod, szProcessName, sizeof(szProcessName)/sizeof(char));
                    }
                }
                
                Process p = {pid, "", usage};
                strncpy(p.name, szProcessName, sizeof(p.name) - 1);
                p.name[sizeof(p.name) - 1] = '\0';

                for (int k = 0; k < 5; k++) {
                    if (p.usage > top5[k].usage) {
                        for (int j = 4; j > k; j--) {
                            top5[j] = top5[j - 1];
                        }
                        top5[k] = p;
                        break;
                    }
                }
            }
            CloseHandle(hProcess);
        }
    }
    
    memcpy(tracked_processes, new_tracked, new_tracked_count * sizeof(ProcessTracker));
    tracked_process_count = new_tracked_count;

    printf("Top Processes:\n\n");
    for (int i = 0; i < 5; i++) {
        if (top5[i].pid > 0) {
            printf("%d. %s (%lu) - %.1f%%\n", i + 1, top5[i].name, top5[i].pid, top5[i].usage);
        }
    }
}

int main() {
    init_system_info();

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    while (1) {
        printf("\033[H\033[J");
        fflush(stdout);

        ULARGE_INTEGER sys_diff;
        sys_diff.QuadPart = 0;
        double cpu_usage = get_cpu_usage(&sys_diff);

        printf("---\n\n");
        printf("CPU Usage: %.1f%%\n", cpu_usage);
        print_memory_usage();
        print_disk_usage();
        print_network_usage();
        printf("----------------------------------------\n\n");

        print_top_processes(sys_diff);

        Sleep(1000);
    }

    return 0;
}
