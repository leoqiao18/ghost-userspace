import subprocess
import sys
import time
import signal

INTERVAL = 1
BASE_WATTS = 1.0
all_procs_to_cleanup = []


def handle_sigint(sig, frame):
    for p in all_procs_to_cleanup:
        p.kill()
    
    exit(0)

def clean():
    # subprocess.Popen(["sudo", "./clear-enclaves.sh"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    # subprocess.Popen(["sudo", "rm", "-f", "/sys/fs/bpf/pid_to_consumption", "/sys/fs/bpf/energy_snapshot"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    subprocess.Popen(["sudo", "rm", "-f", "/sys/fs/bpf/pid_to_consumption", "/sys/fs/bpf/energy_snapshot"])

def start_shiv(procs):
    # p = subprocess.Popen(["sudo", "bazel-bin/agent_efs", "--base_watts", str(BASE_WATTS), "--ghost_cpus", "0"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    p = subprocess.Popen(["sudo", "efs_test/shiv/shiv.out", str(procs[0].pid), str(procs[1].pid)])
    all_procs_to_cleanup.append(p)

def spawn_tasks():
    def taskset(pid, cpu):
        subprocess.Popen(["taskset", "-cp", str(cpu), str(pid)])

    cmds = [
        "eas_test/no-op",
        ["python", "eas_test/cpu.py"],
        # "eas_test/large_mem",
    ]
    procs = []

    
    for cmd in cmds:
        # p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        all_procs_to_cleanup.append(p)
        taskset(p.pid, 0)
        procs.append(p)
    
    return procs

    
def start_tracker(procs):
    p = subprocess.Popen(["sudo", "efs_test/process_energy_tracker/process_energy_tracker.out", str(INTERVAL), str(procs[0].pid), str(procs[1].pid), sys.argv[1]])
    all_procs_to_cleanup.append(p)
    p.wait()

def main():
    try:
        signal.signal(signal.SIGINT, handle_sigint)
        signal.signal(signal.SIGTERM, handle_sigint)
        clean()
        time.sleep(1)
        procs = spawn_tasks()
        start_shiv(procs)
        time.sleep(1)
        for p in procs:
            print(p.pid)
        start_tracker(procs)
        handle_sigint(None, None)
    except Exception as e:
        handle_sigint(None, None)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} FILENAME")
        exit(1)
    main()