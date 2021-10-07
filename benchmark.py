#!/usr/bin/env python3

import argparse
import os
import re
import subprocess

from testrun import eprint, testrun
from utils.stats import print_stats


def get_num_threads():
    """
    Get the value of `NUM_THREADS` if defined, else get the number of CPUs from `lscpu`
    """
    num_threads = os.environ.get("NUM_THREADS")
    if not num_threads:
        num_threads = subprocess.check_output("lscpu | grep ^CPU\(s\)", shell=True).split()[1]
    return int(num_threads)


def benchmark(cmd, repetitions=10, show_statistics=False):
    num_threads = get_num_threads()
    logfile = os.path.join("benchmark.output", os.path.basename(cmd[0]))
    if len(cmd) > 1:
        logfile += "_" + "_".join(cmd[1:])
    logfile += f"_{num_threads:02}.log"

    with open(logfile, "w") as file:
        eprint(f"NUM_THREADS={num_threads} ", end='')
        testrun(cmd, repetitions, stdout=file)

    runtimes = []
    with open(logfile) as file:
        elapsed = re.compile(r"[Ee]lapsed wall time: (\d+(?:\.\d*)?)")
        for line in file:
            match = re.search(elapsed, line)
            if match:
                runtimes.append(float(match.group(1)))

    if show_statistics:
        print_stats(runtimes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument("-r", "--repetitions",
                        type=int, default=10,
                        help="number of repetitions (default is 10)",
                        required=False)

    parser.add_argument("-s", "--show-statistics",
                        action="store_true",
                        help="show summary statistics",
                        required=False)

    parser.add_argument("cmd",
                        nargs="*",
                        help="program to run")

    args = parser.parse_args()

    os.makedirs("benchmark.output", exist_ok=True)

    if args.cmd:
        benchmark(args.cmd, args.repetitions, args.show_statistics)
    else:
        # Read commands from file
        with open("benchmark.input") as file:
            for line in file:
                cmd = line.split()
                benchmark(cmd, args.repetitions, args.show_statistics)
