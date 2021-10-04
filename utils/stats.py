#!/usr/bin/env python3

import statistics
import sys


def mean_rsd(numbers):
    """
    Calculate mean and relative standard deviation (RSD) of a series of numbers
    """
    mean = statistics.fmean(numbers)
    sd = statistics.stdev(numbers)
    rsd = 100 * sd / abs(mean)
    return mean, rsd


def quartiles(numbers):
    return statistics.quantiles(numbers, n=4, method='inclusive')


def deciles(numbers):
    return statistics.quantiles(numbers, n=10, method='inclusive')


if __name__ == "__main__":
    numbers = []
    for line in sys.stdin:
        numbers.append(float(line))

    Q = quartiles(numbers)
    D = deciles(numbers)
    assert(Q[1] == statistics.median(numbers))
    assert(D[4] == statistics.median(numbers))

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
        D[-1] - D[0],                 # Central 80 % range
        max(numbers) - min(numbers)   # Total range
    ]

    print(",".join(headers))
    print(",".join(map(lambda stat: f"{stat:.2f}", stats)), end='')
    # Append mean ± relative standard deviation in percent
    print(",{:.2f} ± {:.2f} %".format(*mean_rsd(numbers)))
