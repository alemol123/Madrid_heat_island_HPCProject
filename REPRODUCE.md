# Reproducing our results

Every number in the report comes from a command listed here. Each person adds the
exact commands for their experiments when they land (P1 maintains this file).

## 1. Get the code and environment

```bash
git clone https://github.com/alemol123/Madrid_heat_island_HPCProject.git
cd Madrid_heat_island_HPCProject
git checkout <tag used in the report>
source env/modules.sh cpu      # see env/README.md for the cluster
```

Never compile or run experiments on the login node (`rust`); `-march=native` must
see the compute node's CPU, so the Slurm scripts compile inside the job.

## 2. Check correctness (on a compute node)

```bash
srun -p cpu -n 1 --time=00:10:00 bash -c "source env/modules.sh cpu && make test && make check"
```

`make test` runs the unit tests (uniformity, symmetry, heat conservation, stability
check, file formats). `make check` compares the serial output with
`tests/ref/ref_seed42_256x256_200.bin` and every other built version with serial.

## 3. Serial solver (P1)

Single run:

```bash
./build/heat_serial --nx 2048 --ny 2048 --steps 2000 --city synthetic --seed 42 \
    --out results/field.bin --timing results/runs.csv --tag baseline
```

| Experiment | Command | Output |
|---|---|---|
| Baseline, 3 sizes × 3 repeats | `sbatch slurm/p1_baseline.sh` | rows tagged `baseline` in `results/runs.csv`, log in `results/logs/` |
| Problem-size sensitivity 256–8192 | `sbatch slurm/p1_sizes.sh` | rows tagged `sizes` |
| Compiler flags before/after | `sbatch slurm/p1_flags.sh` | rows tagged `flags_*`, vectorization reports in `build/<jobid>/*/vec.txt` |
| Serial baseline with the AVX-512 kernel (reference for all parallel speed-ups) | `sbatch --export=ALL,TAG=baseline_avx512 slurm/p1_baseline.sh` | rows tagged `baseline_avx512` |
| Kernel code variants (+ perf counters if available) | `sbatch slurm/p1_opt.sh` | rows tagged `opt_*` (version `serial-opt`); each variant is checked against the reference kernel |
| Halo-exchange tests for MPI (1, 2, 3, 5 ranks over 2 nodes) + `make check` | `sbatch slurm/p1_halo.sh` | log in `results/logs/p1_halo_*.out`; locally: `make test-mpi` |

Jobs used in the report: 27714 (baseline), 27715 (sizes), 27716 (flags) at commit 36cd286, and
27718 (code variants) at commit 520c642; 7 Nov 2026, node fortran. Figures and tables:

```bash
python3 analysis/p1_plots.py          # -> results/plots/p1_sizes.png, p1_flags.png, p1_tables.md
```

The reference file in `tests/ref/` was made with:

```bash
./build/heat_serial --nx 256 --ny 256 --steps 200 --city synthetic --seed 42 \
    --out tests/ref/ref_seed42_256x256_200.bin
```

## 4. Other versions

(P2, P3, P4, P5: add your commands here.)

### MPI (P3)

The account limit is 64 CPUs per user, and with `--hint=nomultithread` each rank takes
a whole core (2 CPUs). So the sweeps use at most 32 ranks; `scaling_mpi.sh` skips the
64-rank point by itself.

| Experiment | Command | Output |
|---|---|---|
| Correctness, with and without `--overlap` (1–4 ranks, bitwise vs serial) | `sbatch slurm/p3_overlap_check.sh` | job 31598, ALL PASSED |
| Strong scaling, 1 node, 1–32 ranks, 4096², 2000 steps, 3 repeats | `sbatch --ntasks=32 --export=ALL,TAG=mpi_strong,CSV=results/p3_runs.csv slurm/scaling_mpi.sh` | job 31601, tag `mpi_strong` |
| Same, 2 nodes (16 ranks per node) | `sbatch --nodes=2 --ntasks=32 --ntasks-per-node=16 --export=ALL,TAG=mpi_strong_2node,CSV=results/p3_runs.csv slurm/scaling_mpi.sh` | job 31602, tag `mpi_strong_2node` |
| Overlap, 1 node | `sbatch --ntasks=32 --export=ALL,TAG=mpi_overlap,EXTRA=--overlap,CSV=results/p3_runs.csv slurm/scaling_mpi.sh` | job 31603, tag `mpi_overlap` |
| Overlap, 2 nodes | `sbatch --nodes=2 --ntasks=32 --ntasks-per-node=16 --export=ALL,TAG=mpi_overlap_2node,EXTRA=--overlap,CSV=results/p3_runs.csv slurm/scaling_mpi.sh` | job 31604, tag `mpi_overlap_2node` |

All four sweeps ran on 17 Nov 2026 at commit adc54c8. Figures and tables:

```bash
python3 analysis/plots.py speedup --csv results/p3_runs.csv --tags mpi_strong mpi_overlap --x ranks --labels "1 node" "1 node + overlap" --title "MPI strong scaling, 4096x4096, 1 node" --out results/plots/p3_strong_1node.png
python3 analysis/plots.py speedup --csv results/p3_runs.csv --tags mpi_strong mpi_strong_2node --x ranks --labels "1 node" "2 nodes" --title "MPI strong scaling: 1 vs 2 nodes" --out results/plots/p3_1vs2_nodes.png
python3 analysis/plots.py speedup --csv results/p3_runs.csv --tags mpi_strong_2node mpi_overlap_2node --x ranks --labels "2 nodes" "2 nodes + overlap" --title "MPI strong scaling, 4096x4096, 2 nodes" --out results/plots/p3_strong_2node.png
python3 analysis/plots.py breakdown --csv results/p3_runs.csv --tags mpi_strong mpi_overlap mpi_strong_2node mpi_overlap_2node --out results/plots/p3_breakdown.png
```
