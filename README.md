# Tasking 2.0

An evolution of the task-parallel runtime system/library developed in my
[thesis][1]

## Build
On x86-64 GNU/Linux:
```console
$ cd src
$ make
(...)
```

## Test
```console
$ ./testrun.sh -d
./spc 10000 1: .......... ✔
./bpc 10000 9 1: .......... ✔
./bpc 10000 9 10 1: .......... ✔
./fib-like 30 1: .......... ✔
```

## Benchmark
```console
$ NUM_THREADS=4 ./benchmark.sh -s ./fib-like 30 1
NUM_THREADS=4 ./fib-like 30 1: .......... ✔
Min     | P10     | P25     | Median  | P75     | P90     | Max     | P75-P25 | P90-P10 | Max-Min | Mean ± RSD
336.710 | 336.715 | 336.720 | 336.730 | 336.730 | 336.800 | 336.860 | 0.010   | 0.085   | 0.150   | 336.739 ± 0.01 %
```

## High-level Overview
![](overview.png)

<!-- References -->

[1]: https://epub.uni-bayreuth.de/2990
