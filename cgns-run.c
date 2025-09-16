#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sched.h>
#include <dirent.h>
#include <getopt.h>

static const char *namespaces[] = {
    "mnt", "uts", "ipc", "pid", "net", "user", "cgroup", NULL
};

typedef struct {
    char namespaces[7][256];
    char cgroups[64][512];
    int num_cgroups;
} process_info_t;

int get_namespace_info(pid_t pid, process_info_t *info) {
    char ns_path[256];
    char link_target[256];
    ssize_t link_len;

    for (int i = 0; namespaces[i]; i++) {
        snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/%s", pid, namespaces[i]);

        link_len = readlink(ns_path, link_target, sizeof(link_target) - 1);
        if (link_len == -1) {
            if (errno == ENOENT) {
                strcpy(info->namespaces[i], "not available");
            } else {
                snprintf(info->namespaces[i], sizeof(info->namespaces[i]),
                        "error reading (%s)", strerror(errno));
            }
        } else {
            link_target[link_len] = '\0';
            strcpy(info->namespaces[i], link_target);
        }
    }
    return 0;
}

int get_cgroup_info(pid_t pid, process_info_t *info) {
    char cgroup_path[512];
    FILE *cgroup_file;
    char line[512];

    snprintf(cgroup_path, sizeof(cgroup_path), "/proc/%d/cgroup", pid);

    cgroup_file = fopen(cgroup_path, "r");
    if (!cgroup_file) {
        info->num_cgroups = 0;
        return -1;
    }

    info->num_cgroups = 0;
    while (fgets(line, sizeof(line), cgroup_file) && info->num_cgroups < 64) {
        line[strcspn(line, "\n")] = 0;
        strcpy(info->cgroups[info->num_cgroups], line);
        info->num_cgroups++;
    }

    fclose(cgroup_file);
    return 0;
}

void print_namespace_info(pid_t pid) {
    process_info_t info;
    get_namespace_info(pid, &info);

    printf("Process %d namespaces:\n", pid);
    for (int i = 0; namespaces[i]; i++) {
        printf("  %s: %s\n", namespaces[i], info.namespaces[i]);
    }
}

void print_cgroup_info(pid_t pid) {
    process_info_t info;
    get_cgroup_info(pid, &info);

    printf("\nProcess %d cgroups:\n", pid);
    if (info.num_cgroups == 0) {
        printf("  Error reading cgroups\n");
        return;
    }

    for (int i = 0; i < info.num_cgroups; i++) {
        printf("  %s\n", info.cgroups[i]);
    }
}

void diff_processes(pid_t pid1, pid_t pid2) {
    process_info_t info1, info2;

    if (get_namespace_info(pid1, &info1) != 0 || get_namespace_info(pid2, &info2) != 0) {
        fprintf(stderr, "Failed to get namespace info\n");
        return;
    }

    if (get_cgroup_info(pid1, &info1) != 0 || get_cgroup_info(pid2, &info2) != 0) {
        fprintf(stderr, "Warning: Failed to get cgroup info for one or both processes\n");
    }

    printf("Comparing processes %d and %d:\n\n", pid1, pid2);

    printf("NAMESPACES:\n");
    int ns_same = 1;
    for (int i = 0; namespaces[i]; i++) {
        int same = strcmp(info1.namespaces[i], info2.namespaces[i]) == 0;
        printf("  %s: %s\n", namespaces[i], same ? "SAME" : "DIFFERENT");
        if (!same) {
            printf("    PID %d: %s\n", pid1, info1.namespaces[i]);
            printf("    PID %d: %s\n", pid2, info2.namespaces[i]);
            ns_same = 0;
        }
    }

    printf("\nCGROUPS:\n");
    int cg_same = 1;

    if (info1.num_cgroups != info2.num_cgroups) {
        printf("  Different number of cgroups (%d vs %d)\n", info1.num_cgroups, info2.num_cgroups);
        cg_same = 0;
    }

    int max_cgroups = info1.num_cgroups > info2.num_cgroups ? info1.num_cgroups : info2.num_cgroups;
    for (int i = 0; i < max_cgroups; i++) {
        if (i < info1.num_cgroups && i < info2.num_cgroups) {
            int same = strcmp(info1.cgroups[i], info2.cgroups[i]) == 0;
            if (same) {
                printf("  [%d]: SAME\n", i);
            } else {
                printf("  [%d]: DIFFERENT\n", i);
                printf("    PID %d: %s\n", pid1, info1.cgroups[i]);
                printf("    PID %d: %s\n", pid2, info2.cgroups[i]);
                cg_same = 0;
            }
        } else if (i < info1.num_cgroups) {
            printf("  [%d]: Only in PID %d: %s\n", i, pid1, info1.cgroups[i]);
            cg_same = 0;
        } else {
            printf("  [%d]: Only in PID %d: %s\n", i, pid2, info2.cgroups[i]);
            cg_same = 0;
        }
    }

    printf("\nSUMMARY:\n");
    printf("  Namespaces: %s\n", ns_same ? "ALL SAME" : "SOME DIFFERENT");
    printf("  Cgroups: %s\n", cg_same ? "ALL SAME" : "SOME DIFFERENT");
}

int join_namespaces(pid_t target_pid) {
    char ns_path[256];
    int fd;

    for (int i = 0; namespaces[i]; i++) {
        snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/%s", target_pid, namespaces[i]);

        fd = open(ns_path, O_RDONLY);
        if (fd == -1) {
            if (errno == ENOENT) {
                continue;
            }
            fprintf(stderr, "Failed to open %s: %s\n", ns_path, strerror(errno));
            return -1;
        }

        if (setns(fd, 0) == -1) {
            if (strcmp(namespaces[i], "user") == 0) {
                fprintf(stderr, "Warning: Failed to join user namespace: %s\n", strerror(errno));
                close(fd);
                continue;
            }
            fprintf(stderr, "Failed to join namespace %s: %s\n", namespaces[i], strerror(errno));
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

int join_cgroups(pid_t target_pid) {
    char cgroup_path[512];
    char target_cgroup_path[512];
    char my_pid_str[32];
    FILE *target_cgroup_file;
    char line[512];
    char *colon1, *colon2;
    int fd;

    snprintf(my_pid_str, sizeof(my_pid_str), "%d\n", getpid());
    snprintf(target_cgroup_path, sizeof(target_cgroup_path), "/proc/%d/cgroup", target_pid);

    target_cgroup_file = fopen(target_cgroup_path, "r");
    if (!target_cgroup_file) {
        fprintf(stderr, "Failed to open %s: %s\n", target_cgroup_path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), target_cgroup_file)) {
        line[strcspn(line, "\n")] = 0;

        colon1 = strchr(line, ':');
        if (!colon1) continue;

        colon2 = strchr(colon1 + 1, ':');
        if (!colon2) continue;

        *colon1 = '\0';
        *colon2 = '\0';

        char *subsystems = colon1 + 1;
        char *cgroup_name = colon2 + 1;

        if (strlen(subsystems) == 0) {
            snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup%s/cgroup.procs", cgroup_name);
        } else {
            snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/%s%s/cgroup.procs", subsystems, cgroup_name);
        }

        fd = open(cgroup_path, O_WRONLY);
        if (fd == -1) {
            continue;
        }

        if (write(fd, my_pid_str, strlen(my_pid_str)) == -1) {
            close(fd);
            continue;
        }

        close(fd);
    }

    fclose(target_cgroup_file);
    return 0;
}

void usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] <target_pid> [command] [args...]\n", prog_name);
    fprintf(stderr, "       %s -d <pid1> <pid2>\n", prog_name);
    fprintf(stderr, "Execute command in the same namespaces and cgroups as target_pid\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -l, --list    List namespaces and cgroups info for the target PID\n");
    fprintf(stderr, "  -d, --diff    Compare namespaces and cgroups between two PIDs\n");
    fprintf(stderr, "  -h, --help    Show this help message\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s -l 1234              # List info for process 1234\n", prog_name);
    fprintf(stderr, "  %s -d 1234 5678         # Compare processes 1234 and 5678\n", prog_name);
    fprintf(stderr, "  %s 1234 ps aux          # Run 'ps aux' in same context as 1234\n", prog_name);
}

int main(int argc, char *argv[]) {
    int opt;
    int list_only = 0;
    int diff_mode = 0;

    static struct option long_options[] = {
        {"list", no_argument, 0, 'l'},
        {"diff", no_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "ldh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'l':
                list_only = 1;
                break;
            case 'd':
                diff_mode = 1;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (diff_mode) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Diff mode requires two PIDs\n");
            usage(argv[0]);
            return 1;
        }

        pid_t pid1 = atoi(argv[optind]);
        pid_t pid2 = atoi(argv[optind + 1]);

        if (pid1 <= 0 || pid2 <= 0) {
            fprintf(stderr, "Invalid PIDs: %s %s\n", argv[optind], argv[optind + 1]);
            return 1;
        }

        char proc_path1[64], proc_path2[64];
        snprintf(proc_path1, sizeof(proc_path1), "/proc/%d", pid1);
        snprintf(proc_path2, sizeof(proc_path2), "/proc/%d", pid2);

        struct stat st;
        if (stat(proc_path1, &st) == -1) {
            fprintf(stderr, "Process %d not found\n", pid1);
            return 1;
        }
        if (stat(proc_path2, &st) == -1) {
            fprintf(stderr, "Process %d not found\n", pid2);
            return 1;
        }

        diff_processes(pid1, pid2);
        return 0;
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    pid_t target_pid = atoi(argv[optind]);
    if (target_pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", argv[optind]);
        return 1;
    }

    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", target_pid);
    struct stat st;
    if (stat(proc_path, &st) == -1) {
        fprintf(stderr, "Process %d not found\n", target_pid);
        return 1;
    }

    if (list_only) {
        print_namespace_info(target_pid);
        print_cgroup_info(target_pid);
        return 0;
    }

    if (optind + 1 >= argc) {
        fprintf(stderr, "No command specified\n");
        usage(argv[0]);
        return 1;
    }

    if (join_namespaces(target_pid) == -1) {
        fprintf(stderr, "Failed to join namespaces\n");
        return 1;
    }

    if (join_cgroups(target_pid) == -1) {
        fprintf(stderr, "Warning: Failed to join some cgroups\n");
    }

    execvp(argv[optind + 1], &argv[optind + 1]);
    fprintf(stderr, "Failed to exec %s: %s\n", argv[optind + 1], strerror(errno));
    return 1;
}