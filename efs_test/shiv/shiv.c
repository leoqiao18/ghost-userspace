#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include "shiv.h"


struct bpf_object *load_bpf_obj(char *prog_name)
{
    // Load and verify BPF application
    fprintf(stderr, "Loading BPF code in memory\n");
    struct bpf_object *obj = bpf_object__open_file(prog_name, NULL);
    if (libbpf_get_error(obj))
    {
        fprintf(stderr, "ERROR: opening BPF object file failed\n");
        return NULL;
    }

    // Load BPF program
    fprintf(stderr, "Loading and verifying the code in the kernel\n");
    if (bpf_object__load(obj))
    {
        fprintf(stderr, "ERROR: loading BPF object file failed\n");
        return NULL;
    }

    return obj;
}

struct bpf_map *load_bpf_map(struct bpf_object *obj, char *map_name)
{
    struct bpf_map *map = bpf_object__find_map_by_name(obj, map_name);
    if (libbpf_get_error(map))
    {
        fprintf(stderr, "ERROR: finding BPF map failed\n");
        return NULL;
    }
    return map;
}

int create_perf_event(struct bpf_map *map, int type, uint32_t config, uint32_t idx)
{
    int map_fd = bpf_map__fd(map);
    
    struct perf_event_attr attr = {
        .type = type,
        .config = config,
        .size = sizeof(struct perf_event_attr)
    };

    // TODO: only assuming a single socket at CPU "0"
    int perf_fd = syscall(__NR_perf_event_open, &attr, -1 /*pid*/, 1 /*cpu*/, -1 /*group_fd*/, 0 /*flags*/);
    if (perf_fd < 0)
    {
        fprintf(stderr, "ERROR: Failed to create perf event\n");
        return -1;
    }

    if (bpf_map_update_elem(map_fd, &idx, &perf_fd, BPF_ANY) < 0) {
        fprintf(stderr, "ERROR: putting perf_event_fd to map failed\n");
        close(perf_fd);
        return -1;
    }

    return perf_fd;
}

struct bpf_link *attach_bpf_prog_to_sched_switch(const struct bpf_object *obj, const char* prog_name)
{
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, prog_name);
    if (libbpf_get_error(prog))
    {
        fprintf(stderr, "ERROR: finding BPF program failed\n");
        return NULL;
    }
    struct bpf_link *link;
    link = bpf_program__attach_tracepoint(prog, "sched", "sched_switch");
    if (libbpf_get_error(link))
    {
        fprintf(stderr, "ERROR: Attaching perf event to BPF program failed\n");
        return NULL;
    }

    return link;
}

int main(int argc, char *argv[])
{
    struct bpf_object *obj;
    struct bpf_link *link;
    struct bpf_map *perf_event_descriptors_map;
    int perf_fd;
    int type;
    pid_t target_pids[2];
    FILE *power_type;

    // Parse cli arguments
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }

    target_pids[0] = atoi(argv[1]);
    target_pids[1] = atoi(argv[2]);

    // read power type from file
    power_type = fopen("/sys/bus/event_source/devices/power/type", "r");
    if (power_type == NULL)
    {
        return 1;
    }
    fscanf(power_type, "%d", &type);
    fclose(power_type);

    // load bpf object
    if ((obj = load_bpf_obj("/home/flq2101/dev/github/leoqiao18/ghost-userspace/efs_test/shiv/shiv.bpf.o")) == NULL)
    {
        return 1;
    }

    // load bpf map
    if ((perf_event_descriptors_map = load_bpf_map(obj, "perf_event_descriptors")) == NULL)
    {
        goto cleanup_obj;
    }

    // create perf events and put into bpf map
    // TODO: assuming only a single socket "0"
    if((perf_fd = create_perf_event(perf_event_descriptors_map, type, PERF_COUNT_ENERGY_PKG, 0)) < 0)
    {
        goto cleanup_obj;
    }

    if((perf_fd = create_perf_event(perf_event_descriptors_map, type, PERF_COUNT_ENERGY_RAM, 1)) < 0)
    {
        goto cleanup_obj;
    }
    
    // attach bpf program
    if ((link = attach_bpf_prog_to_sched_switch(obj, "shiv_handle_sched_switch")) == NULL)
    {
        goto cleanup_perf;
    }


    int pid_to_consumption_fd = bpf_map__fd(load_bpf_map(obj, "pid_to_consumption"));
    int energy_snapshot_fd = bpf_map__fd(load_bpf_map(obj, "energy_snapshot"));

    struct task_consumption empty = {0, 0, 0};
    bpf_map_update_elem(pid_to_consumption_fd, &target_pids[0], &empty, BPF_ANY);
    bpf_map_update_elem(pid_to_consumption_fd, &target_pids[1], &empty, BPF_ANY);

    int ret;
    ret = bpf_obj_pin(pid_to_consumption_fd, "/sys/fs/bpf/pid_to_consumption");
    if (ret) {
        perror("Failed to pin map");
        close(pid_to_consumption_fd);
        exit(1);
    }
    ret = bpf_obj_pin(energy_snapshot_fd, "/sys/fs/bpf/energy_snapshot");

  if (ret) {
    perror("Failed to pin map");
    close(energy_snapshot_fd);
    exit(1);
  }
    while (1)
    {
        sleep(1);
    }


cleanup_perf:
    close(perf_fd);
cleanup_obj:
    bpf_object__close(obj);
    return 0;
}
