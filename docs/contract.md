# Shared contract

Agreed by all five on Wed 4 November 2026. P1 keeps it up to date.
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

---

## To decide in the Wed 4 meeting

Write the decision next to each item, then delete this heading.

1. **Stability bound.** With the *average* form above, the scheme is stable for `alpha_c <= 1`;
   the plan's `alpha_c <= 0.25` belongs to the *sum* form `alpha_c*(sum of 4 neighbours - 4T)`.
   Proposal: keep the average form, require `alpha_c + cool_c <= 1` for every type (checked at startup).
   Decision: ______
2. **Verification tolerance.** 1e-5 absolute is below float32 precision at about 300 K (one ULP is about 3e-5),
   so OpenMP SIMD, FMA and the GPU cannot pass it.
   Proposal: `compare.py` passes when max |a - b| <= 1e-5 * max|T| (relative); MPI must also be bitwise identical.
   Decision: ______
3. **Boundary condition.** Proposal: fixed edges at T_air (Dirichlet). MPI edge ranks use MPI_PROC_NULL and
   keep fixed ghost rows. Alternative: insulated edges.
   Decision: ______
4. **Units and time.** Temperatures in °C or K? Steps per simulated day (solar period)?
   Proposal: °C, T_init = T_air = 25, one day = 1000 steps, solar(t) = S0 * max(0, sin(2*pi*t/1000)).
   Decision: ______
5. **Coefficient table** (alpha, absorb, cool per type) in src/params.h. P1 proposes values by Thu 5;
   all versions include that header. Decision: ______
6. **Branches and reviews.** Branch names p1-solver, p2-data, p3-mpi, p4-openmp, p5-gpu; pull request
   to main with 1 approval from the review-chain reviewer. Decision: ______
