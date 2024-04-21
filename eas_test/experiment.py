#!/usr/bin/env python3
import subprocess
import re
import sys
import signal
import json


def scaphandre():
    cmd = "scaphandre --no-headers json -s 1"
    # run cmd and get stdout as file object
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    # parse the content of p as a series of json objects
    for line in p.stdout:
        try:
            data = json.loads(line)
            print(data)
        except json.JSONDecodeError as e:
            print(f"Error decoding json: {e}")


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
        for p in procs:
            p.kill()

    return _handle_sigint


def main():
    cmds = ["./loop.py", "./sleep.py"]

    procs = []
    signal.signal(signal.SIGINT, handle_sigint(procs))

    for cmd in cmds:
        p = subprocess.Popen(cmd)
        procs.append(p)
        register_to_enclave(p.pid)
        print(f"Registered pid={p.pid} ({cmd})")

    signal.pause()


if __name__ == "__main__":
    # main()
    scaphandre()
