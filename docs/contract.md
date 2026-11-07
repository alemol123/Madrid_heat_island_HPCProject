# Shared contract

Agreed Thu 5 November 2026 (decisions at the end). P1 keeps it up to date.
Change it only with everyone's agreement (open a pull request and tag all five).

## Repository layout

```
src/serial/  src/mpi/  src/hybrid/  src/openacc/   one folder per version
src/params.h                                     shared constants (only place they are defined)
tools/       gen_city.py compare.py make_scenarios.py run_ensemble.* score_candidates.c
slurm/       serial.sh mpi.sh hybrid.sh gpu.sh scaling_*.sh ensemble.sh
env/         modules.sh README.md                compiler, modules, versions, flags
data/        small samples + download scripts (no large files)
results/     CSV, plots, logs/ (job logs, sacct output)
analysis/    plots.py
tests/       unit tests, tests/ref/ small reference outputs
docs/        report, eurohpc_plan, slides, contract.md
```

## Command line (identical for every version)

```
./heat --nx 2048 --ny 2048 --steps 2000 --city synthetic --seed 42 \
       --out results/field.bin --timing results/runs.csv --tag baseline
```

`--city` is either `synthetic` (generated from `--seed`) or a path to a city file.

## Grid and indexing

- `ny` rows by `nx` columns. Cell (i, j), column i and row j, is stored at `j*nx + i` (row-major).
- MPI decomposes by rows (strips) first; blocks later.

## Cell types (int8)

| Value | Type |
|---|---|
| 0 | road |
| 1 | building |
| 2 | grass / park |
| 3 | tree |
| 4 | water |
| 5 | green roof |

## File formats

- **City file:** `nx`, `ny` as int32 (little-endian), then `nx*ny` int8 cell types, row-major.
- **Output field:** `nx`, `ny` as int32, then `nx*ny` float32 temperatures, row-major.
- **Checksum:** sum of all cells accumulated in a `double`, printed as `checksum %.10e`.
  It is a quick sanity check only; `tools/compare.py` is the real test.

## Heat update (explicit)

```
T_new = T + alpha_c*(avg of 4 neighbours - T) + solar(t)*absorb_c - cool_c*(T - T_air)
```

The kernel is one isolated function `step(in, out, ...)` with two buffers (ping-pong).
OpenMP, MPI and OpenACC all wrap it unchanged.

## Kernel interface (src/common/step.h, src/common/io.h)

- Shared code lives in `src/common/` (kernel, file I/O, synthetic city, CSV, timer); every version links it.
- Temperature arrays are **padded with one ghost cell on each side**: (nx+2) × (ny+2).
  Interior cell (i, j) is at `IDX(i, j, nx) = (j+1)*(nx+2) + (i+1)`. The city array is not padded.
- `step()` writes only interior cells and reads ghosts. The caller fills ghosts first:
  `apply_boundary()` on real domain edges, halo exchange between MPI ranks.
- `step_rows(..., j0, j1)` updates rows j0..j1-1 only (for the MPI overlap version).
- On CPUs with AVX-512 (the cluster, built with `-march=native`) the kernel looks up the coefficients with
  vector-register permutes; elsewhere it uses the scalar table lookup. Both are bitwise identical
  (`step_kernel_name()` says which one a build uses; `-DSTEP_SCALAR` forces the scalar one).
  OpenMP versions should parallelize over rows by calling `step_rows()` per thread, so they keep the fast kernel.
- `--city synthetic`: each cell's type is a pure function of (i, j, seed), so any rank can generate
  its own rows and get the same city as serial. It is a minimal stand-in; P2's `tools/gen_city.py`
  makes the realistic cities.

## Results CSV (one row per run, header written if the file is new)

```
tag,version,nx,ny,steps,nodes,ranks,threads,gpus,time_total,time_compute,time_comm,time_io,time_transfer,checksum,git_commit,slurm_jobid,node
```

- `time_compute`: the time loop only. `time_comm`: MPI halo exchange (0 if not MPI).
- `time_io`: reading the city and writing the output file.
- `time_transfer`: CPU <-> GPU data copies (OpenACC); 0 for CPU versions. Not part of `time_compute`.
- `time_total`: the whole run, start to finish (includes all of the above).
- `node`: the compute node name (`SLURMD_NODENAME`), quoted; MPI runs list every node used,
  comma-separated. Needed because the GPUs differ (Ada on haskell, Blackwell on julia/fortran)
  and nodes are shared with other jobs.
- Write the CSV through `append_csv()` in `src/common/io.c` so every version produces the same columns.

## Compiler flags

`gcc -O3 -march=native` (+ `-fopenmp`); `mpicc` for MPI. Exact flags and module versions in `env/README.md`.
Compile on a compute node (or inside the job), because `-march=native` depends on the CPU.

## Experiments

At least 3 repetitions per configuration; report mean and spread.

## Decisions (Thu 5 November 2026)

1. **Stability.** We use the *average* form of the update. Every cell type must satisfy
   `alpha_c >= 0`, `cool_c >= 0`, `absorb_c >= 0` and `alpha_c + cool_c <= 1`; the solver checks this at
   start-up (`check_stability()`) and refuses to run otherwise. (The plan's `alpha <= 0.25` belongs to the
   *sum* form, `alpha*(sum of 4 neighbours - 4T)`, which we do not use.)
2. **Verification tolerance.** `tools/compare.py` passes when `max|a - b| <= 1e-5 * max|T|` (relative).
   MPI must additionally be bitwise identical to serial (`compare.py --exact`), because it performs the
   same operations in the same order. OpenMP, hybrid and OpenACC use the relative tolerance (SIMD, FMA and
   GPU change the order of floating-point operations).
3. **Boundary condition.** Fixed edges at `T_air` (Dirichlet). MPI ranks on the top/bottom edge of the domain
   use `MPI_PROC_NULL` for the missing neighbour and fill that ghost row with `apply_boundary()`.
   Insulated edges (`--bc neumann`) exist only for the heat-conservation test.
4. **Units and time.** Temperatures in degrees C. `T_init = T_air = 25`. One simulated day = 1000 steps;
   `solar(t) = S0 * max(0, sin(2*pi*t/1000))`, so noon is step 250 and a 2000-step run is two days.
5. **Coefficients.** The table in `src/params.h` (peak heating above air: road +10, building +7.5,
   grass +1.3, tree +0.4, water +0.25, green roof +0.75 C). These are plausible model values, not
   measurements; the report says so. Changing them means rerunning every experiment, so they are frozen.
6. **Git workflow.** Branches `p1-solver`, `p2-data`, `p3-mpi`, `p4-openmp`, `p5-gpu`. No direct pushes to
   `main`; every change goes through a pull request with 1 approval from the review chain
   (P1 reviews P2, P2 reviews P3, P3 reviews P4, P4 reviews P5, P5 reviews P1).
   Small environment/setup changes may be approved by the repo owner (P1).