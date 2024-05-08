
import subprocess
import time
import signal
import sys

BASE_WATTS = 8.0
all_procs_to_cleanup = []

def handle_sigint(sig, frame):
    for p in all_procs_to_cleanup:
        p.kill()
    
    exit(0)

def clean():
    # subprocess.Popen(["sudo", "./clear-enclaves.sh"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    # subprocess.Popen(["sudo", "rm", "-f", "/sys/fs/bpf/pid_to_consumption", "/sys/fs/bpf/energy_snapshot"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    subprocess.Popen(["sudo", "./clear-enclaves.sh"])
    subprocess.Popen(["sudo", "rm", "-f", "/sys/fs/bpf/pid_to_consumption", "/sys/fs/bpf/energy_snapshot"])

def start_ghost():
    # p = subprocess.Popen(["sudo", "bazel-bin/agent_efs", "--base_watts", str(BASE_WATTS), "--ghost_cpus", "0"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    p = subprocess.Popen(["sudo", "bazel-bin/agent_efs", "--base_watts", str(BASE_WATTS), "--ghost_cpus", "0-1", "--min_granularity", "2ms"])
    all_procs_to_cleanup.append(p)

def spawn_efs_tasks():
    def taskset(pid, cpu):
        subprocess.Popen(["taskset", "-cp", str(cpu), str(pid)])

    def register_to_enclave(pid):
        try:
            # Writing pid to the file as root user
            subprocess.run(
                ["sudo", "bash", "-c", f"echo {pid} > /sys/fs/ghost/enclave_1/tasks"],
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print(f"Error writing pid {pid}: {e}")

    cmds = [
        # "eas_test/no-op",
        # "eas_test/large_mem"
        # ["python3", "efs_test/mem.py"],
        # ["python3", "efs_test/crypto.py"],
        ["python3", "efs_test/throughput_crypto.py"],
        ["python3", "efs_test/throughput_crypto.py"],
    ]
    procs = []

    
    for cmd in cmds:
        # p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        all_procs_to_cleanup.append(p)
        taskset(p.pid, 0)
        register_to_enclave(p.pid)
        procs.append(p)
    
    return procs

def spawn_cfs_tasks():
    def taskset(pid, cpu):
        subprocess.Popen(["taskset", "-cp", str(cpu), str(pid)])

    cmds = [
        # "eas_test/no-op",
        # "eas_test/large_mem"
        # ["python3", "efs_test/mem.py"],
        # ["python3", "efs_test/crypto.py"],
        ["python3", "efs_test/throughput_crypto.py"],
        ["python3", "efs_test/throughput_crypto.py"],
    ]
    procs = []

    
    for cmd in cmds:
        # p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        all_procs_to_cleanup.append(p)
        taskset(p.pid, 0)
        procs.append(p)
    
    return procs

    
def main():
    try:
        # CFS
        start_time = time.time()
        procs = spawn_cfs_tasks()
        for p in procs:
            p.wait()
        print(f"CFS time: {time.time() - start_time}")

        # EFS
        signal.signal(signal.SIGINT, handle_sigint)
        signal.signal(signal.SIGTERM, handle_sigint)
        clean()
        time.sleep(1)
        start_ghost()
        time.sleep(1)
        start_time = time.time()
        procs = spawn_efs_tasks()
        for p in procs:
            p.wait()
        print(f"EFS time: {time.time() - start_time}")
        handle_sigint(None, None)
    except Exception as e:
        handle_sigint(None, None)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} BASE_WATTS")
        exit(1)
    BASE_WATTS = float(sys.argv[1])
    main()
