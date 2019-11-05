## List of Microbenchmarks

- **BPC**, short for **B**ouncing **P**roducer-**C**onsumer benchmark, as far
  as I know, first described by [Dinan et al][1]. There are two types of
  tasks, producer and consumer tasks. Each producer task creates another
  producer task followed by *n* consumer tasks, until a certain depth *d* is
  reached. Consumer tasks run for *t* microseconds. The smaller the values of
  *n* and *t*, the harder it becomes to exploit the available parallelism. A
  solid contender for the most antagonistic microbenchmark.

- **Cilksort**, a recursive algorithm that sorts an array of *n* integers by
  dividing it into four sub-arrays, recursively sorting these, and merging the
  sorted results back together in a divide-and-conquer style. For sub-arrays
  &le; 1024 elements, the algorithm performs a sequential quick sort with
  median-of-three pivot selection and partitioning. For even smaller
  sub-arrays &le; 20 elements, the algorithm falls back to using insertion
  sort. As the name suggests, the code is based on a benchmark distributed
  with [MIT Cilk][2].

- **Fibonacci**, the mother of all microbenchmarks. No evaluation is complete
  without it.

- **Fibonacci-like** tree recursion, a variation on the mother of all
  microbenchmarks, where leaf tasks perform some computation for *t*
  microseconds before returning. This simulates a cut-off, as if tasks were
  inlined after reaching a certain recursion depth.

- **Quicksort**, a well-known recursive algorithm that performs an in-place
  sort of an array of *n* integers by partitioning it into two sub-arrays
  according to some pivot element and recursively sorting the sub-arrays. The
  pivot is chosen as the median of the first, middle, and last array elements.
  For sub-arrays &le; 100 elements, the algorithm falls back to using
  insertion sort, which is usually faster on small inputs.

- **SPC**, short for **S**imple **P**roducer-**C**onsumer benchmark. A single
  worker produces *n* tasks, each running for *t* microseconds. A walk in the
  park compared to **BPC**, unless *t* is very small.

Still TODO

## Licenses

- [Cilksort](http://supertech.lcs.mit.edu/cilk/index.html)
- [N-Body](https://benchmarksgame-team.pages.debian.net/benchmarksgame/license.html)
- [N-Queens](https://github.com/bsc-pm/bots/blob/master/LICENSE)
- [UTS](https://sourceforge.net/p/uts-benchmark/code/ci/master/tree/LICENSE)

<!-- References -->

[1]: https://dl.acm.org/citation.cfm?id=1654113
[2]: http://supertech.lcs.mit.edu/cilk/index.html
