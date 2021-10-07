#!/usr/bin/env python3

import argparse
import statistics
import sys

import tabulate as T


def mean_rsd(numbers):
    """
    Calculate mean and relative standard deviation (RSD) of a series of numbers
    """
    mean = statistics.fmean(numbers)
    sd = statistics.stdev(numbers)
    rsd = 100 * sd / abs(mean)
    return mean, rsd


def quartiles(numbers):
    return statistics.quantiles(numbers, n=4, method="inclusive")


def deciles(numbers):
    return statistics.quantiles(numbers, n=10, method="inclusive")


def print_stats(numbers, tabulate=True):
    Q = quartiles(numbers)
    D = deciles(numbers)

    headers = [
        "Min", "P10", "P25", "Median", "P75", "P90", "Max",
        "P75-P25", "P90-P10", "Max-Min", "Mean ± RSD"
    ]

    stats = [
        min(numbers),                 # Minimum
        D[0],                         # 10th percentile / 1st decile
        Q[0],                         # 25th percentile / 1st quartile
        Q[1],                         # 50th percentile / 2nd quartile / median
        Q[-1],                        # 75th percentile / 3rd quartile
        D[-1],                        # 90th percentile / 9th decile
        max(numbers),                 # Maximum
        Q[-1] - Q[0],                 # Interquartile range
        D[-1] - D[0],                 # Interdecile range
        max(numbers) - min(numbers)   # Total range
    ]

    if tabulate:
        stats = [round(stat, 2) for stat in stats]
        # Append mean ± relative standard deviation in percent
        stats.append("{:.2f} ± {:.2f} %".format(*mean_rsd(numbers)))
        print(T.tabulate([stats], headers=headers, tablefmt="pretty"))
    else:
        print(",".join(headers))
        print(",".join(map(lambda stat: f"{stat:.2f}", stats)), end='')
        # Append mean ± relative standard deviation in percent
        print(",{:.2f} ± {:.2f} %".format(*mean_rsd(numbers)))



if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tabulate", action="store_true", help="tabulate output", required=False)
    args = parser.parse_args()

    numbers = []
    for line in sys.stdin:
        numbers.append(float(line))

    print_stats(numbers, args.tabulate)
