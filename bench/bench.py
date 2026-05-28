#!/usr/bin/env python3

import subprocess
import sys
import os

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))


def run(script):
    path = os.path.join(BENCH_DIR, script)
    print(f"\n=== {script} ===\n")
    sys.stdout.flush()
    r = subprocess.run([sys.executable, path], check=False)
    return r.returncode


if __name__ == "__main__":
    ok = True

    if run("race.py") != 0:
        print("race.py FAILED", file=sys.stderr)
        ok = False

    if run("throughput.py") != 0:
        print("throughput.py FAILED", file=sys.stderr)
        ok = False

    if ok:
        run("plot.py")

    sys.exit(0 if ok else 1)
