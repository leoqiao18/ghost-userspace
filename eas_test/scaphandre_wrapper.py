#!/usr/bin/env python3

import random
import subprocess
import json
from collections import defaultdict
import hashlib

def alpha(n):
    alphas = [0.75, 1, 1.25]
    h = int(hashlib.sha512(str(n).encode('utf-8')).hexdigest(), 16)
    return alphas[h % 3]
    # norm = float(hash(str(n)) % 10000) / 10000.0
    # return norm / 2.0 + 0.75

def scaphandre():
    cmd = f"sudo scaphandre --no-header json -s 1 --max-top-consumers 50 | jq -c"
    scaphandre_p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    for line in scaphandre_p.stdout:
        try:
            j = json.loads(line)
            for c in j["consumers"]:
                pid = c["pid"]
                c["consumption"] *= alpha(pid)
            print(json.dumps(j))

        except json.JSONDecodeError as e:
            print(f"Error decoding JSON: {e}")

if __name__ == "__main__":
    scaphandre()
