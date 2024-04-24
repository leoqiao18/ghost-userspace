#!/usr/bin/env python3
from collections import defaultdict
import subprocess
import re
import time
import sys
import signal
import json
from scaphandre_wrapper import alpha
import matplotlib.pyplot as plt

_pid_to_energy = defaultdict(lambda: [])
pid_to_energy = defaultdict(lambda: 0)
scaphandre_p = None


def scaphandre(pids):
    cmd = f"./scaphandre_wrapper.py"
    global scaphandre_p 
    scaphandre_p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    for line in scaphandre_p.stdout:
        try:
            j = json.loads(line)
            for c in j["consumers"]:
                pid, consumption = c["pid"], c["consumption"]
                if pid in pids:
                    pid_to_energy[pid] = consumption

            print("Energy consumption (microjoules):")
            for pid, consumption in pid_to_energy.items():
                print(f"{pid} -> {consumption}")

            if len(pid_to_energy) == len(pids):
                for pid, energy in pid_to_energy.items():
                    _pid_to_energy[pid].append(energy)

        except json.JSONDecodeError as e:
            print(f"Error decoding JSON: {e}")


def register_to_enclave(pid):
    try:
        # Writing pid to the file as root user
        subprocess.run(
            ["sudo", "bash", "-c", f"echo {pid} > /sys/fs/ghost/enclave_1/tasks"],
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"Error writing pid {pid}: {e}")


def handle_sigint(procs):

    # use _pid_to_energy to plot
    

    def _handle_sigint(sig, frame):
        for pid, lst in _pid_to_energy.items():
            plt.plot(lst, label=str(pid))
        plt.savefig('energy_consumption_test.png')
        plt.show()
        
        if scaphandre_p:
            scaphandre_p.kill()
        for p in procs:
            p.kill()

    return _handle_sigint

def taskset(pid, cpu):
    subprocess.Popen(["taskset", "-cp", str(cpu), str(pid)] , stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def main():
    cmds = [
        # ["python3", "graphics.py"],
        # ["python3", "graphics.py"],
        # ["python3", "io.py"],
        # ["python3", "io.py"],
        ("./simd", 1),
        # (["python3", "cpu.py"], 1),
        ("./no-op", 1),
        ("./large_mem", 1),
        # "./mem",
        # "./mult",
        # ["python3", "cpu.py"],
        # ["python3", "cpu.py"],
        #  "./simd",
        # "./no-op",
        #  "./simd"
        ]

    procs = []
    signal.signal(signal.SIGINT, handle_sigint(procs))
    signal.signal(signal.SIGTERM, handle_sigint(procs))

    for cmd, cpu in cmds:
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        taskset(p.pid, cpu)
        procs.append(p)
        register_to_enclave(p.pid)
        print(f"Registered pid={p.pid} ({cmd}) (alpha: {alpha(p.pid)})")

    scaphandre([p.pid for p in procs])

    signal.pause()


if __name__ == "__main__":
    main()
