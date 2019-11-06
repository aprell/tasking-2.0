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

- **LU**, a blocked LU decomposition of a sparse *N* &#10005; *N* matrix of
  doubles, partitioned into (*N*/*B*)<sup>2</sup> *B* &#10005; *B* blocks. The
  block size *B* determines the task granularity and must divide *N*. The
  sparsity of the matrix &mdash; the fraction or percentage of blocks that
  contain only zeros &mdash; increases with the number of blocks in each
  dimension. Blocks of zeros are not allocated. The code is based on an old
  benchmark written for Cell Superscalar (CellSs), back in the days when the
  Cell processor was still around. A more recent version of this benchmark
  uses OpenMP and is included in the [Barcelona OpenMP Tasks Suite (BOTS)][3].

- **MM**, a blocked matrix multiplication of two *N* &#10005; *N* matrices of
  doubles, each partitioned into (*N*/*B*)<sup>2</sup> *B* &#10005; *B*
  blocks. The block size *B* determines the task granularity and must divide
  *N*.

- **MM-DAC**, a divide-and-conquer *O*(*n*<sup>3</sup>) matrix
  multiplication of two *N* &#10005; *N* matrices of doubles. The cut-off that
  ends the recursive subdivision determines the number of tasks and the task
  granularity. To keep things simple, *N* must be a power of two.

- **N-Body**, a simple simulation of 1024 planets, adapted from the [Computer
  Language Benchmarks Game][4] (formerly called The Great Computer Language
  Shootout).

- **N-Queens**, a recursive backtracking algorithm that finds all possible
  solutions to the *N*-Queens problem of placing *N* queens on an *N* &#10005;
  *N* chessboard such that no queen can attack other queens. The code is based
  on the OpenMP version from the [Barcelona OpenMP Tasks Suite (BOTS)][3],
  which in turn is based on the version distributed with [MIT Cilk][2].

- **Quicksort**, a well-known recursive algorithm that performs an in-place
  sort of an array of *n* integers by partitioning it into two sub-arrays
  according to some pivot element and recursively sorting the sub-arrays. The
  pivot is chosen as the median of the first, middle, and last array elements.
  For sub-arrays &le; 100 elements, the algorithm falls back to using
  insertion sort, which is usually faster on small inputs.

- **SPC**, short for **S**imple **P**roducer-**C**onsumer benchmark. A single
  worker produces *n* tasks, each running for *t* microseconds. A walk in the
  park compared to **BPC**, unless *t* is very small.

- **UTS**, the **U**nbalanced **T**ree **S**earch benchmark that enumerates
  all nodes in a highly unbalanced tree, devised by [Olivier et al][5]. The
  tree is generated implicitly: each child node is constructed from the SHA-1
  hash of the parent node and child index. The code is based on the Pthreads
  version from the [UTS repository][6].

There are many more microbenchmarks that could be added, for example, programs
from the [Problem-Based Benchmark Suite (PBBS)][7].

## Licenses

- [Cilksort](http://supertech.lcs.mit.edu/cilk/index.html)
- [N-Body](https://benchmarksgame-team.pages.debian.net/benchmarksgame/license.html)
- [N-Queens](https://github.com/bsc-pm/bots/blob/master/LICENSE)
- [UTS](https://sourceforge.net/p/uts-benchmark/code/ci/master/tree/LICENSE)

<!-- References -->

[1]: https://dl.acm.org/citation.cfm?id=1654113
[2]: http://supertech.lcs.mit.edu/cilk/index.html
[3]: https://github.com/bsc-pm/bots
[4]: https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html
[5]: https://www.cs.unc.edu/~olivier/LCPC06.pdf
[6]: https://sourceforge.net/projects/uts-benchmark
[7]: http://www.cs.cmu.edu/~pbbs
