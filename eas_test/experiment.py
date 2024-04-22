#!/usr/bin/env python3
from collections import defaultdict
import subprocess
import re
import sys
import signal
import json

INTERVAL = 1
pid_to_energy = defaultdict(lambda: 0)
scaphandre_p = None


def scaphandre(pids):
    cmd = f"sudo scaphandre --no-header json -s {INTERVAL} --max-top-consumers 50 | jq -c"
    global scaphandre_p 
    scaphandre_p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    for line in scaphandre_p.stdout:
        try:
            j = json.loads(line)
            for c in j["consumers"]:
                pid, consumption = c["pid"], c["consumption"]
                if pid in pids:
                    pid_to_energy[pid] += consumption * INTERVAL

            print("Energy consumption (microjoules):")
            for pid in pids:
                print(f"{pid} -> {pid_to_energy[pid]}")

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
    def _handle_sigint(sig, frame):
        if scaphandre_p:
            scaphandre_p.kill()
        for p in procs:
            p.kill()

    return _handle_sigint


def main():
    cmds = [
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./io.py",
        "./cpu.py",
        "./cpu.py",
        "./cpu.py",
        "./cpu.py",
        "./cpu.py"
        ]

    procs = []
    signal.signal(signal.SIGINT, handle_sigint(procs))
    signal.signal(signal.SIGTERM, handle_sigint(procs))

    for cmd in cmds:
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        procs.append(p)
        register_to_enclave(p.pid)
        print(f"Registered pid={p.pid} ({cmd})")

    scaphandre([p.pid for p in procs])

    signal.pause()


if __name__ == "__main__":
    main()
