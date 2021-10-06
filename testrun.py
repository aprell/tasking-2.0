#!/usr/bin/env python3

import argparse
import subprocess
import sys


RESET = "\033[0m"
BOLD = "\033[1m"
FAIL = "\033[31m"
PASS = "\033[32m"


def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def testrun(cmd, repetitions=10, discard_output=False):
    success = True
    eprint(" ".join(cmd) + ": ", end='')

    try:
        for _ in range(repetitions):
            eprint(".", end='')
            subprocess.run(cmd, check=True, capture_output=discard_output)
    except subprocess.CalledProcessError:
        success = False

    eprint(f"{BOLD}{FAIL} X{RESET}" if not success else "")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-d", "--discard-output", action="store_true", help="discard output", required=False)
    parser.add_argument("-r", "--repetitions", type=int, default=10, help="number of repetitions (default is 10)", required=False)
    parser.add_argument("cmd", help="program to run", nargs="*")
    args = parser.parse_args()

    if args.cmd:
        testrun(args.cmd, args.repetitions, args.discard_output)
    else:
        # Read commands from file
        with open("testrun.input") as file:
            for line in file:
                cmd = line.split()
                testrun(cmd, args.repetitions, args.discard_output)
