#include "bpf/user/agent.h"
#include "schedulers/eas_bpf/eas_bpf.skel.h"

int main(int argc, char *argv[]) {
  struct eas_bpf *skel;
  int err;

  skel = eas_bpf__open();
  if (!skel) {
    fprintf(stderr, "Failed to open BPF\n");
    return 1;
  }

  err = eas_bpf__load(skel);
  if (err) {
    fprintf(stderr, "Failed to load BPF skeleton\n");
    return 1;
  }

  err = eas_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "Failed to attach BPF skeleton\n");
    return 1;
  }

  while (1) {
    u32 zero = 0;
    u64 energy;
    if (bpf_map_lookup_elem(skel->maps.energy, &zero, &energy) < 0) {
      fprintf(stderr, "Failed to lookup energy\n");
      break;
    }

    printf("Energy: %llu\n", energy);
  }

  eas_bpf__destroy(skel);
  return 0;
}
