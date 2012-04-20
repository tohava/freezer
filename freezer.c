#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#define MAX_PROCESS_COUNT 262144
#define WANTED_FREE_PERCENTS 70.0

struct process_info {
    pid_t pid;
    long rss;
};

struct processes {
    struct process_info data[MAX_PROCESS_COUNT];
    size_t count;
    long page_size;
    size_t mem_total;
};

void add_process(struct processes *processes, pid_t pid) {
#define ent processes->data[processes->count]
    FILE *f;
    char str[512];
    
    sprintf(str, "/proc/%d/stat", pid);
    ent.pid = pid;
    
    f = fopen(str, "r");
    fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %ld", &ent.rss);
    fclose(f);
    processes->count++;
#undef ent
}

uid_t get_pid_uid(pid_t pid) {
    struct stat stat_buf;
    char str[512];
    sprintf(str, "/proc/%d", pid);
    stat(str, &stat_buf);
    return stat_buf.st_uid;
}

long get_meminfo_key(char *key)
{
    long result = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    char buf[512];
    long kibis;
    while (fgets(buf, sizeof(buf) / sizeof(char), f) && !result)
    {
        char *tok = strtok(buf, " ");
        if (!strcmp(tok, key))
        {
            if (!(tok = strtok(NULL, " ")) ||
                sscanf(tok, "%ld", &kibis) == 0 ||
                !(tok = strtok(NULL, " ")) ||
                !strcmp(tok, "kB"))
            {
                fprintf(stderr, "Malformed MemTotal from /proc/mem_info\n");
                exit(1);
            }
            else
            {
                result = kibis * 1024;
            }
        }
    }
    fclose(f);
    if (!result) {
        fprintf(stderr, "Did not find MemTotal entry in /proc/mem_info\n");
        exit(1);
    }
    return result;
}

void init_processes(struct processes *processes)
{
    processes->page_size = sysconf(_SC_PAGESIZE);
    processes->mem_total = get_meminfo_key("MemTotal:");
    processes->count = 0;
}

pid_t sscan_pid(char *str)
{
    pid_t pid;
#define CASE(type,format) case sizeof(type): sscanf(str, format, &pid); break
    switch (sizeof(pid_t)) {
        CASE(char,"%hhu");
        CASE(short, "%hu");
        CASE(int, "%u");
        CASE(long, "%lu");
        default:
            exit(1);
    }
    return pid;
#undef CASE
}

double pages_to_percents(struct processes *processes, long rss) {
    return 100.0 * rss / (processes->mem_total / processes->page_size);
}

void print_processes(struct processes *processes)
{
    size_t i;
    printf("Total memory: %ld\n", processes->mem_total);
    printf("Page size: %ld\n", processes->page_size);
    for (i = 0; i < processes->count; i++)
        printf("Process %d - rss %ld, mem %f\n",
               processes->data[i].pid,
               processes->data[i].rss,
               pages_to_percents(processes, processes->data[i].rss));
}

int compare_rss(const void *arg1, const void *arg2)
{
    long rss1 = ((struct process_info*)arg1)->rss;
    long rss2 = ((struct process_info*)arg2)->rss;
    return rss1 < rss2 ? -1 : rss1 == rss2 ? 0 : +1;
}

void sort_processes_by_rss(struct processes *processes) {
    qsort(processes->data, processes->count, sizeof(struct process_info),
          compare_rss);
}

void stop_processes(struct processes *processes, double wanted_free_percents) {
#define GMK get_meminfo_key
    long taken_pages = ((processes->mem_total -
                         GMK("MemFree:") - GMK("Buffers:") - GMK("Cached:")) /
                        processes->page_size);
    double taken_percents = pages_to_percents(processes, taken_pages);
    double how_much_to_free = taken_percents - (100.0 - wanted_free_percents);
    size_t i;
    // printf("Taken pages is %ld percents is %f\n", taken_pages, taken_percents);
    for (i = processes->count; i > 0 && how_much_to_free > 0.0; --i) {
#define P processes->data[i - 1]
        how_much_to_free -= pages_to_percents(processes, P.rss);
        // printf("will kill %d\n", P.pid);
        kill(P.pid, SIGSTOP);
#undef P
    }
#undef GMK
}

int main() {
    DIR *proc_root; 
    struct dirent *proc_entry;
    uid_t uid = getuid();
    struct processes processes;
    init_processes(&processes);

    
    proc_root = opendir("/proc");
    for (proc_entry = readdir(proc_root);
         proc_entry != NULL;
         proc_entry = readdir(proc_root))
    {
        pid_t pid;
        if ((pid = sscan_pid(proc_entry->d_name)) &&
            get_pid_uid(pid) == uid)
        {
            add_process(&processes, pid);
        }
    }
    sort_processes_by_rss(&processes);
    // print_processes(&processes);
    stop_processes(&processes, WANTED_FREE_PERCENTS);
    return 0;
}
