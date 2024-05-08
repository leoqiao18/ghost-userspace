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

#define BASE_WATTS (655)

struct task_consumption {
    uint64_t energy;
    uint64_t time;
    uint64_t timestamp;
};

struct energy_snapshot {
    uint64_t energy;
    uint64_t timestamp;
};

int main(int argc, char *argv[])
{
    pid_t target_pids[2];

    // Parse cli arguments
    if (argc != 6)
    {
        fprintf(stderr, "usage: %s INTERVAL PID1 PID2 FILENAME ITER\n", argv[0]);
        return 1;
    }

    int interval = atoi(argv[1]);
    target_pids[0] = atoi(argv[2]);
    target_pids[1] = atoi(argv[3]);
    char *filename = argv[4];
    int iterations = atoi(argv[5]);

    int pid_to_consumption_fd = bpf_obj_get("/sys/fs/bpf/pid_to_consumption");
    if (pid_to_consumption_fd < 0) {
        perror("Failed to open pid_to_consumption map");
        exit(1);
    }
    int energy_snapshot_fd = bpf_obj_get("/sys/fs/bpf/energy_snapshot");
    if (energy_snapshot_fd < 0) {
        perror("Failed to open energy_snapshot map");
        exit(1);
    }

    struct task_consumption consumption_value[2];
    struct energy_snapshot energy_snapshot_value;
    uint32_t energy_snapshot_key = 0;
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    FILE *scale_file = fopen("/sys/bus/event_source/devices/power/events/energy-pkg.scale", "r");

    double scale;
    fscanf(scale_file, "%lf", &scale);
    fclose(scale_file);

    uint64_t system_energy_baseline;
    uint64_t system_time_baseline;
    uint64_t energy_baseline[2];
    uint64_t time_baseline[2];

    for (int i = 0; i < iterations + 5; i++) {
        if (bpf_map_lookup_elem(energy_snapshot_fd, &energy_snapshot_key, &energy_snapshot_value) < 0) {
            perror("Failed to lookup energy_snapshot map");
            exit(1);
        }
        // printf("Energy snapshot at index 0: %llu\n", energy_snapshot_value);

        // read from pid_to_consumption map
        if (bpf_map_lookup_elem(pid_to_consumption_fd, &target_pids[0], &consumption_value[0]) < 0) {
            perror("Failed to lookup pid_to_consumption map");
            exit(1);
        }
        if (bpf_map_lookup_elem(pid_to_consumption_fd, &target_pids[1], &consumption_value[1]) < 0) {
            perror("Failed to lookup pid_to_consumption map");
            exit(1);
        }

	if (i == 0) {
	    system_energy_baseline = energy_snapshot_value.energy;
	    system_time_baseline = energy_snapshot_value.timestamp;
	    energy_baseline[0] = consumption_value[0].energy;
	    energy_baseline[1] = consumption_value[1].energy;
	    time_baseline[0] = consumption_value[0].time;
	    time_baseline[1] = consumption_value[1].time;
        } else if (i <= 5) {
        } else {
            fprintf(file, "%lu, %lu, %lu, %lu, %lu, %lu\n", 
			    energy_snapshot_value.energy - system_energy_baseline, energy_snapshot_value.timestamp - system_time_baseline,
			    consumption_value[0].energy - energy_baseline[0], consumption_value[0].time - time_baseline[0], 
                            consumption_value[1].energy - energy_baseline[1], consumption_value[1].time - time_baseline[1]);                                               
            fflush(file);
	}
        usleep(interval);
    }
}
