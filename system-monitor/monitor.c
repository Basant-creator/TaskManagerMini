#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/statvfs.h>

int max_pids = 4194304;
unsigned long long *prev_proc_ticks = NULL;
unsigned long long prev_sys_idle = 0;
unsigned long long prev_sys_total = 0;
int num_cores = 1;

typedef struct {
    int pid;
    char name[256];
    double usage;
} Process;

void init_system_info() {
    num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores < 1) num_cores = 1;

    FILE *f_pid = fopen("/proc/sys/kernel/pid_max", "r");
    if (f_pid) {
        if (fscanf(f_pid, "%d", &max_pids) != 1) {
            max_pids = 4194304;
        }
        fclose(f_pid);
    }
    prev_proc_ticks = calloc(max_pids + 1, sizeof(unsigned long long));
}

double get_cpu_usage(unsigned long long *sys_diff_out) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0.0;

    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    char line[256];

    if (fgets(line, sizeof(line), f)) {
        sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    }
    fclose(f);

    unsigned long long current_idle = idle + iowait;
    unsigned long long current_total = user + nice + system + current_idle + irq + softirq + steal;

    unsigned long long total_diff = current_total - prev_sys_total;
    unsigned long long idle_diff = current_idle - prev_sys_idle;

    prev_sys_total = current_total;
    prev_sys_idle = current_idle;

    *sys_diff_out = total_diff;

    if (total_diff == 0) return 0.0;
    return (100.0 * (total_diff - idle_diff)) / total_diff;
}

void print_memory_usage() {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    unsigned long long mem_total = 0, mem_avail = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%llu", &mem_total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%llu", &mem_avail);
        }
    }
    fclose(f);

    double gb_total = mem_total / 1048576.0;
    double gb_avail = mem_avail / 1048576.0;
    double gb_used = gb_total - gb_avail;
    double pct = mem_total > 0 ? (gb_used / gb_total) * 100.0 : 0.0;

    printf("Memory Usage: %.2f GB / %.2f GB (%.1f%%)\n", gb_used, gb_total, pct);
}

void print_disk_usage() {
    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0) {
        unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
        unsigned long long free = (unsigned long long)vfs.f_bfree * vfs.f_frsize;
        unsigned long long used = total - free;
        double pct = total > 0 ? ((double)used / total) * 100.0 : 0.0;
        printf("Disk Usage: %.1f%%\n", pct);
    } else {
        printf("Disk Usage: N/A\n");
    }
}

void print_network_usage() {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;

    unsigned long long rx_bytes = 0, tx_bytes = 0;
    char line[256];

    fgets(line, sizeof(line), f); 
    fgets(line, sizeof(line), f); 

    while (fgets(line, sizeof(line), f)) {
        char iface[32];
        unsigned long long rx, tx;
        if (sscanf(line, "%31s %llu %*s %*s %*s %*s %*s %*s %*s %llu", iface, &rx, &tx) == 3) {
            if (strncmp(iface, "lo", 2) != 0) {
                rx_bytes += rx;
                tx_bytes += tx;
            }
        }
    }
    fclose(f);
    printf("Network: Sent: %llu KB | Received: %llu KB\n", tx_bytes / 1024, rx_bytes / 1024);
}

void print_top_processes(unsigned long long total_sys_diff) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    struct dirent *ent;
    Process top5[5] = {0};

    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit(ent->d_name[0])) continue;

        int pid = atoi(ent->d_name);
        if (pid <= 0 || pid > max_pids) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;

        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), f)) {
            char *p_open = strchr(buffer, '(');
            char *p_close = strrchr(buffer, ')');
            
            if (p_open && p_close && p_open < p_close) {
                char comm[256];
                int len = p_close - p_open - 1;
                if (len >= sizeof(comm)) len = sizeof(comm) - 1;
                strncpy(comm, p_open + 1, len);
                comm[len] = '\0';

                char *rest = p_close + 2;
                unsigned long long utime = 0, stime = 0;
                
                if (sscanf(rest, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %llu", &utime, &stime) == 2) {
                    unsigned long long proc_ticks = utime + stime;
                    double usage = 0.0;

                    if (prev_proc_ticks[pid] != 0 && total_sys_diff > 0) {
                        unsigned long long proc_diff = 0;
                        if (proc_ticks >= prev_proc_ticks[pid]) {
                            proc_diff = proc_ticks - prev_proc_ticks[pid];
                        }
                        usage = 100.0 * num_cores * proc_diff / total_sys_diff;
                    }
                    prev_proc_ticks[pid] = proc_ticks;

                    Process p = {pid, "", usage};
                    strncpy(p.name, comm, sizeof(p.name) - 1);

                    for (int i = 0; i < 5; i++) {
                        if (p.usage > top5[i].usage) {
                            for (int j = 4; j > i; j--) {
                                top5[j] = top5[j - 1];
                            }
                            top5[i] = p;
                            break;
                        }
                    }
                }
            }
        }
        fclose(f);
    }
    closedir(dir);

    printf("Top Processes:\n\n");
    for (int i = 0; i < 5; i++) {
        if (top5[i].pid > 0) {
            printf("%d. %s (%d) - %.1f%%\n", i + 1, top5[i].name, top5[i].pid, top5[i].usage);
        }
    }
}

int main() {
    init_system_info();

    if (!prev_proc_ticks) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return 1;
    }

    while (1) {
        printf("\033[H\033[J");
        fflush(stdout);

        unsigned long long sys_diff = 0;
        double cpu_usage = get_cpu_usage(&sys_diff);

        printf("---\n\n");
        printf("CPU Usage: %.1f%%\n", cpu_usage);
        print_memory_usage();
        print_disk_usage();
        print_network_usage();
        printf("----------------------------------------\n\n");

        print_top_processes(sys_diff);

        sleep(1);
    }

    free(prev_proc_ticks);
    return 0;
}
