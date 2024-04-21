#!/usr/bin/env python3
import subprocess
import re
import sys
import signal
import json


def scaphandre():
    cmd = "scaphandre --no-headers json -s 1"
    # cmd is a command that, when run, will write a series of JSON objects to stdout
    # I want to parse each JSON object and print it to the console
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    while True:
        line = p.stdout.readline()
        if not line:
            break
        try:
            data = json.loads(line)
            print(json.dumps(data, indent=4))
        except json.JSONDecodeError:
            print("Error decoding JSON object")
            continue


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
