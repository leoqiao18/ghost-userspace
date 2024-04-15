#! /usr/bin/env python3
import subprocess
import re
import sys

def run_new_proc(cmd):  
    # Run the command and capture output
    process = subprocess.Popen(cmd, shell=True)
    return process.pid



def register_to_enclave(pid):
    try:
        # Writing pid to the file as root user
        subprocess.run(['sudo', 'bash', '-c', f'echo {pid} > /sys/fs/ghost/enclave_1/tasks'],
                        check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error writing pid {pid}: {e}")


def main():
    cmds = ["./loop.py", "./sleep.py"]
    for cmd in cmds:
        pid = run_new_proc(cmd)
        if pid:
            register_to_enclave(pid)
        else:
            print("Unexpected")
            sys.exit(1)


if __name__ == "__main__":
    main()
