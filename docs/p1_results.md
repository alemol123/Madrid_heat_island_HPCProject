# P1 results: serial baseline, problem size, compiler flags

Draft text for report Section 5.1. Numbers come from `results/runs.csv`
(jobs 27714–27716 and 27718, 7 Nov 2026, node fortran, AMD EPYC 9755 / Zen 5, GCC 13.2.0,
code at commit 36cd286). Regenerate the figures and tables with
`python3 analysis/p1_plots.py`; tables are in `results/plots/p1_tables.md`.

## Setup

Serial solver, synthetic city (seed 42), 2000 steps (two simulated days) unless stated.
Every configuration was run 3 times; we report mean ± standard deviation.
All 54 runs ran on the same node. Repeats gave identical checksums, and run-to-run
variation was below 1.5 % (usually below 0.5 %).

## Baseline

| Grid | Total time (s) | ns per cell update |
|---|---|---|
| 1024 × 1024 | 1.79 ± 0.01 | 0.852 |
| 2048 × 2048 | 7.34 ± 0.02 | 0.873 |
| 4096 × 4096 | 31.60 ± 0.14 | 0.939 |

File output takes under 0.2 % of the run time, so the time loop is what matters.

## Problem-size sensitivity (Figure `results/plots/p1_sizes.png`)

We varied n from 256 to 8192 while keeping about 4·10⁹ cell updates per run.
The time loop touches 9 bytes per cell (two float32 temperature buffers and one int8
cell type), so the working set is about 9n² bytes: it fits in one core's L2 cache
(1 MiB) up to n ≈ 340 and in the L3 cache of one CCD (32 MiB) up to n ≈ 1930.

Time per cell update rises from 0.80 ns (n = 256, in L2) to about 0.81–0.86 ns
(in L3) and 0.93–0.95 ns once the data comes from main memory (n ≥ 4096): only
about 18 % slower, with no sharp jump. At 0.94 ns per update the core moves roughly
10–14 GB/s (9–13 bytes per update, counting the write-allocate of the output
line), well below what one Zen 5 core can stream from memory.

**Interpretation:** the serial kernel is limited by the work per cell, not by memory
bandwidth. 0.8 ns is about 3–4 clock cycles per cell. The likely cause is the
per-cell lookup of the three coefficients (alpha, absorb, cool) by cell type,
which turns into gather operations once the loop is vectorized (see the next
section). With many cores working at once (OpenMP, MPI on one node), the
combined demand will reach the memory-bandwidth limit, so the parallel
versions should become memory-bound even though the serial one is not.

## Compiler flags (Figure `results/plots/p1_flags.png`)

2048 × 2048, 2000 steps. Every variant passed `tools/compare.py` against the -O2 output.

| Flags | Compute (s) | Speed-up vs -O2 | Kernel loop vectorized? |
|---|---|---|---|
| `-O2` | 8.71 | 1.00× | no |
| `-O3` | 7.09 | 1.23× | yes, 16-byte (SSE, 4 floats) |
| `-O3 -march=native` | 7.39 | 1.18× | yes, 64-byte (AVX-512, 16 floats) |
| `-O3 -march=native -ffp-contract=fast` | 6.81 | 1.28× | yes, 64-byte + fused multiply-add |

(Vectorization from the GCC reports `-fopt-info-vec-optimized` in `results/logs/p1_flags_27716.out`;
the kernel's inner loop is `step.c:68`.)

Findings:

- **Vectorization gives the biggest gain (1.23×).** With GCC 13, -O2 does not
  vectorize the kernel loop; -O3 does.
- **Wider vectors do not help.** AVX-512 processes 16 floats per instruction instead
  of 4, yet -march=native was slightly *slower* than plain -O3. This supports the
  conclusion above: arithmetic is not the bottleneck; fetching each cell's
  coefficients from the 6-entry tables (gathers) is.
- **Fused multiply-add helps by 8 %** (-O3 -march=native → + -ffp-contract=fast).
  Because we compile with `-std=c11`, GCC does not fuse operations by default.
  The fused version changes results by up to 4.4·10⁻⁵ (0.0002 %), inside our
  relative tolerance (2.6·10⁻⁴); this is exactly why the team chose a relative
  tolerance.

## Code variants (Figure `results/plots/p1_opt.png`, job p1_opt)

`src/opt/bench_kernels.c` runs six versions of the kernel with identical physics and
checks each against the reference kernel (contract tolerance; "exact" = bitwise identical):

| Variant | Idea | Result vs reference |
|---|---|---|
| reference | `step()`: look up alpha/absorb/cool by cell type for every cell | exact |
| + omp simd | force vectorization, no run-time aliasing checks | exact |
| per-type factors | per step, fold the coefficients into 3 per-type factors (fewer operations) | within tolerance, *not* exact |
| per-cell arrays | store the 3 coefficients per cell: no lookup, 12 more bytes per cell | exact |
| AVX-512 register lookup | 6-entry tables kept in one vector register, looked up with `vpermps` | exact |
| double | same as reference in double precision | within tolerance |

Results on fortran (job 27718, commit 520c642, mean of 3 runs; standard deviation below 0.6 % for every variant):

| Variant | n = 512 (in L3): ns/update | n = 2048 (memory): ns/update | Correct? |
|---|---|---|---|
| reference | 0.809 (1.00×) | 0.888 (1.00×) | exact |
| + omp simd | 0.824 (0.98×) | 0.904 (0.98×) | exact |
| per-type factors | 0.781 (1.04×) | 0.850 (1.05×) | max diff 2.2·10⁻⁴ (tolerance 2.6·10⁻⁴) |
| per-cell arrays | 0.258 (3.13×) | 0.515 (1.72×) | exact |
| **AVX-512 register lookup** | **0.181 (4.48×)** | **0.231 (3.85×)** | **exact** |
| double | 0.891 (0.91×) | 1.067 (0.83×) | max diff 2.5·10⁻⁵ |

Estimated memory traffic (bytes moved per update ÷ time per update; 13 B for the
lookup kernels, 24 B for per-cell arrays, 25 B for double):

| Variant (n = 2048) | GB/s |
|---|---|
| reference | ≈ 15 |
| per-cell arrays | ≈ 49 |
| AVX-512 register lookup | ≈ 56 |

What this shows:

- **The coefficient lookup was the bottleneck.** Replacing the per-cell gathers with
  an in-register permute makes the kernel 3.9–4.5× faster, with bit-for-bit identical
  results. Per-cell arrays remove the lookup too (3.1× while the data fits in cache).
  This confirms the conclusion from the size sweep and the compiler flags.
- **Once the lookup is gone, memory becomes the limit.** The fast variants slow
  down when the data no longer fits in cache (permute 0.18 → 0.23 ns; per-cell arrays
  0.26 → 0.52 ns), and both reach about 50–56 GB/s, which looks like the
  bandwidth one core can draw from memory. Per-cell arrays lose more because they
  move about twice as many bytes per cell. The reference kernel used only about
  15 GB/s, so it was limited by computation. Hardware counters were not available
  (`perf` is not installed on fortran), so these are estimates from bytes per update.
- **`omp simd` changes nothing.** GCC already vectorizes the loop at -O3, so
  forcing it adds no speed; the cost is in the gathers, not in missing vectorization.
- **Algebraically equal is not numerically equal.** The per-type form
  `w·T + α·avg + src` is the same formula rearranged. It is barely faster and much less
  accurate (88 % of the tolerance). The reference adds small corrections to T;
  the rearranged form multiplies the full temperature by a factor and rounds large
  products.
- **Float32 is the right precision.** Double moves twice the data and is 10–17 %
  slower here; its difference from float (2.5·10⁻⁵) is far below what the model's
  coefficients can justify.

**Adopted:** the shared kernel `step()` (src/common/step.c) now uses the register
lookup whenever the build has AVX-512, and falls back to the scalar lookup otherwise.
Both paths, and builds at -O0, -O3 and -O3 -march=native, give bitwise-identical results
(`make test`, `make check`, `compare.py --exact`). All parallel versions therefore start
from the fast kernel, and their speed-ups must be measured against the *new* serial
baseline (tag `baseline_avx512`), not the old one, to be fair.

## Limitations

- One node type (fortran). Nodes are shared (`OverSubscribe=FORCE:2`), but repeats
  agreed within 1.5 %, so interference was small during these runs.
- No hardware counters: `perf` is not installed on fortran (checked by job 27718),
  so bandwidth figures are estimates from bytes per update.
