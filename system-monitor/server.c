#define _WIN32_WINNT 0x0600
#define WINVER 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ERROR_INSUFFICIENT_BUFFER
#define ERROR_INSUFFICIENT_BUFFER 122
#endif

#ifndef NO_ERROR
#define NO_ERROR 0
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")

#define PORT 8080
#define BUFFER_SIZE 4096
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
double last_cpu_usage = 0.0;
Process last_top5[5] = {0};

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
    if (!GetSystemTimes(&ftime, &fsys, &fuser)) return last_cpu_usage;

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

    sys_diff_out->QuadPart = total_diff;

    if (total_diff == 0) return last_cpu_usage;

    prev_sys_idle = current_idle;
    prev_sys_kernel = current_kernel;
    prev_sys_user = current_user;
    
    double usage = (double)(total_diff - idle_diff) * 100.0 / (double)total_diff;
    if (usage < 0.0) usage = 0.0;
    
    last_cpu_usage = usage;
    return usage;
}

double get_memory_usage() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return (double)memInfo.dwMemoryLoad; // Percentage
    }
    return 0.0;
}

double get_disk_usage() {
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExA("C:\\", &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        ULONGLONG used = totalNumberOfBytes.QuadPart - totalNumberOfFreeBytes.QuadPart;
        return ((double)used / (double)totalNumberOfBytes.QuadPart) * 100.0;
    }
    return 0.0;
}

ULONGLONG prev_net_sent = 0;
ULONGLONG prev_net_recv = 0;
ULONGLONG prev_net_time = 0;
double last_sent_mbps = 0.0;
double last_recv_mbps = 0.0;

void get_network_usage(double *sent_mbps, double *recv_mbps) {
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
                
                ULONGLONG current_time = GetTickCount64();
                if (prev_net_time != 0) {
                    ULONGLONG time_diff = current_time - prev_net_time;
                    if (time_diff > 0) {
                        last_recv_mbps = (double)(rx_bytes - prev_net_recv) * 8.0 / 1000000.0 / (time_diff / 1000.0);
                        last_sent_mbps = (double)(tx_bytes - prev_net_sent) * 8.0 / 1000000.0 / (time_diff / 1000.0);
                        if (last_recv_mbps < 0) last_recv_mbps = 0;
                        if (last_sent_mbps < 0) last_sent_mbps = 0;
                    }
                }
                prev_net_recv = rx_bytes;
                prev_net_sent = tx_bytes;
                prev_net_time = current_time;
            }
            free(pIfTable);
        }
    }
    *sent_mbps = last_sent_mbps;
    *recv_mbps = last_recv_mbps;
}

void update_top_processes(ULARGE_INTEGER sys_diff, Process top5[5]) {
    memset(top5, 0, 5 * sizeof(Process));
    if (sys_diff.QuadPart == 0) {
        memcpy(top5, last_top5, 5 * sizeof(Process));
        return;
    }

    DWORD aProcesses[2048], cbNeeded, cProcesses;
    if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) return;
    
    cProcesses = cbNeeded / sizeof(DWORD);
    
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
                if (prev_proc_ticks.QuadPart != 0) {
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
    memcpy(last_top5, top5, 5 * sizeof(Process));
}

void generate_json_response(char *buffer, size_t max_len) {
    ULARGE_INTEGER sys_diff;
    sys_diff.QuadPart = 0;
    
    double cpu = get_cpu_usage(&sys_diff);
    double mem = get_memory_usage();
    double disk = get_disk_usage();
    
    double net_sent_mbps, net_recv_mbps;
    get_network_usage(&net_sent_mbps, &net_recv_mbps);
    
    Process top5[5];
    update_top_processes(sys_diff, top5);

    char proc_json[2048] = "";
    int offset = 0;
    
    for (int i = 0; i < 5; i++) {
        if (top5[i].pid == 0) continue;
        
        char clean_name[256];
        int j = 0;
        for (int k = 0; top5[i].name[k] != '\0' && j < 255; k++) {
            if (top5[i].name[k] == '"' || top5[i].name[k] == '\\') {
                clean_name[j++] = '\\';
            }
            clean_name[j++] = top5[i].name[k];
        }
        clean_name[j] = '\0';
        
        offset += snprintf(proc_json + offset, sizeof(proc_json) - offset,
            "    {\"name\": \"%s\", \"pid\": %lu, \"cpu\": %.1f}%s\n",
            clean_name, top5[i].pid, top5[i].usage, (i < 4 && top5[i+1].pid != 0) ? "," : "");
    }

    snprintf(buffer, max_len,
        "{\n"
        "  \"cpu\": %.1f,\n"
        "  \"memory\": %.1f,\n"
        "  \"disk\": %.1f,\n"
        "  \"network\": {\n"
        "    \"sent_mbps\": %.2f,\n"
        "    \"received_mbps\": %.2f\n"
        "  },\n"
        "  \"processes\": [\n"
        "%s"
        "  ]\n"
        "}",
        cpu, mem, disk, net_sent_mbps, net_recv_mbps, proc_json);
}

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        if (strncmp(buffer, "GET /stats", 10) == 0) {
            char json_payload[BUFFER_SIZE];
            generate_json_response(json_payload, sizeof(json_payload));
            
            char http_response[BUFFER_SIZE * 2];
            snprintf(http_response, sizeof(http_response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Connection: close\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: %zu\r\n"
                "\r\n"
                "%s",
                strlen(json_payload), json_payload);
                
            send(client_socket, http_response, (int)strlen(http_response), 0);
            printf("Served /stats request successfully.\n");
        } else {
            const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client_socket, not_found, (int)strlen(not_found), 0);
            printf("Served 404 for unknown path.\n");
        }
    }
    
    closesocket(client_socket);
}

int main() {
    init_system_info();
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
        return 1;
    }
    
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("Failed to create socket.\n");
        WSACleanup();
        return 1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed on port %d.\n", PORT);
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed.\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    
    printf("HTTP Server running on http://127.0.0.1:%d/\n", PORT);
    printf("Endpoint available at http://127.0.0.1:%d/stats\n", PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        int client_size = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_size);
        
        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed.\n");
            continue;
        }
        
        handle_client(client_socket);
    }
    
    closesocket(server_socket);
    WSACleanup();
    return 0;
}
