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

## Slurm accounting records

Before the freeze (login node, repo root): `bash tools/collect_sacct.sh` writes
`results/sacct_all.txt` and `results/sacct_all_steps.txt` for every job ID found in `results/`.
Jobs of other users that Slurm hides are listed; their owners run the same command and commit.

## 4. Other versions and experiments

Each owner keeps their section up to date. P2's and P4's sections were drafted by P1 from the
scripts, logs and `results/runs.csv` (22 Nov 2026): **P2 and P4, please check and correct them.**

### City data and scenarios (P2)

```bash
# Synthetic cities used in tests and examples (same nx, ny, seed -> same file)
python3 tools/gen_city.py --nx 256 --ny 256 --seed 42 --out data/city_256.city
python3 tests/test_gen_city.py && python3 tests/test_scenarios.py
```

⚠ The Madrid district (`data/district_sol_retiro.city`, 649 × 450 cells at 5 m, from
OpenStreetMap, made by `tools/rasterize_district.py`) exists only on the unmerged
branch `p2-data`. P2: open a PR so the science sweep below can be reproduced from `main`,
and add the rasterizer command here.

| Experiment | Command | Output |
|---|---|---|
| Baseline field of the district | `sbatch --export=ALL,CITY=data/district_sol_retiro.city,STEPS=1420,TAG=district_base,REPS=1 slurm/serial.sh` (P2: confirm) | `build/<jobid>/field.bin` |
| Scenario sweep (60 scenarios: strategies × 5 budgets × 5 seeds) and job lists | `python3 tools/make_sweep.py --city data/district_sol_retiro.city --field build/<jobid>/field.bin` | `data/sweep/` (git-ignored; regenerate) |
| Build the ensemble tools (once) | `sbatch slurm/ens_build.sh` | `build/ens/heat_serial`, `build/ens/run_ensemble` |
| Science sweep with the MPI ensemble runner | `sbatch slurm/ensemble.sh` | fields in the run folder |
| Metrics, table and figure | `python3 tools/summarize_sweep.py --ens <ensemble run folder> --sweep data/sweep/sweep.json` (P2: confirm) | `results/sweep_metrics.csv`, `sweep_summary.csv`, `sweep_cooling.png` |
| Sol vs Retiro comparison | `python3 tools/compare_sol_retiro.py CITY_FILE FIELD_FILE` | printed |
| Ensemble scheduling benchmarks (static / cyclic / dynamic, workers, nodes) | `nohup bash slurm/ens_bench_submit.sh 3 > results/logs/bench_driver.log 2>&1 &` (login node; submits one job at a time) | `results/ensemble.csv` |
| Same job list as a Slurm job array (baseline for the runner) | see `slurm/ensemble_array.sh` header, then `python3 tools/array_wall.py ...` | `results/ensemble.csv` |
| Benchmark figures | `python3 tools/summarize_bench.py --csv results/ensemble.csv` | `results/bench_*.png`, `bench_summary.csv` |
| OpenMP scaling of candidate scoring | `sbatch --export=ALL,CITY=data/city_4096.city,FIELD=build/<jobid>/field.bin slurm/score_scaling.sh` | `results/score_scaling.csv` |
| Scoring figures | `python3 tools/summarize_score.py` | `results/score_scaling.png`, `score_scaling_summary.csv` |

Tests: `bash tests/test_ensemble.sh` (MPI, see header), `python3 tests/test_score.py`,
`python3 tests/test_sweep.py`.

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

⚠ P3's speed-ups are relative to 1 MPI rank on haskell (11.6 s at 4096²); the serial baseline
`baseline_avx512` (job 27721) ran on fortran (8.9 s). The report must say which reference it uses,
or run the serial baseline on haskell as well.

### OpenMP and hybrid MPI + OpenMP (P4)

Every job compiles inside the job and logs its node, ranks and threads at the top of
`results/logs/<name>_<jobid>.out`. All rows are in `results/runs.csv` (version `hybrid`).
Commands reconstructed from the scripts and logs: **P4, please confirm.**

| Experiment | Command | Jobs, tag in `results/runs.csv` |
|---|---|---|
| Correctness (1×4, 2×2, 3×1 ranks × threads vs serial) | `make check` (hybrid slot in `tests/check.sh`) | zero difference |
| OpenMP threads 1, 2, 4, 8 on 1 rank, 4096² | `sbatch --ntasks-per-node=1 --cpus-per-task=T --export=ALL,NX=4096,TAG=p4_omp_T slurm/hybrid.sh` (T = 1, 2, 4, 8) | 33958, 33959, 33961, 33962 (julia), `p4_omp_*` |
| Ranks × threads at 8 cores, 1024² | `sbatch --ntasks-per-node=R --cpus-per-task=T --export=ALL,NX=1024,TAG=p4_hybrid_fixed_RxT slurm/hybrid.sh` (8×1, 4×2, 2×4, 1×8) | 31036, 31037, 31066, 31067 (haskell), `p4_hybrid_fixed_*` |
| 1 node, 8 ranks × 4 threads, 4096² | `sbatch --ntasks-per-node=8 --cpus-per-task=4 --export=ALL,NX=4096,TAG=hybrid-1node-8x4 slurm/hybrid.sh` | 35028 (fortran), `hybrid-1node-8x4` |
| 2 nodes: 4×4, 16×2 and 32×1 per node pair, 4096² | `sbatch --nodes=2 --ntasks-per-node=R --cpus-per-task=T --export=ALL,NX=4096,TAG=... slurm/hybrid.sh` | 35024 (4×4), 35038 (16×2), 35039 (32×1), fortran + haskell |
| 2 nodes, node pair and repeat checks | same as above, 4 ranks/node × 4 threads | 35030 (haskell + julia), 35034 (fortran + haskell repeat) |
| 1D and 2D spatial tiling of the kernel, 4096², 4 threads | `sbatch --export=ALL,VARIANT=baseline slurm/p4_tile_test.sh` (and `VARIANT=tiled`); `sbatch --export=ALL,VARIANT=tiled-2d,TILE_X=X,TILE_Y=Y slurm/p4_tile_2d_test.sh` | 34000, 34001; 34017, 35003–35012, `p4-tile-*` |
| Memory bandwidth (STREAM), single run and thread scaling | `sbatch slurm/p4_stream.sh`; `sbatch slurm/p4_stream_scaling.sh` | 33934–33949; 33955 |
| Vectorization report, node and cache inspection | `sbatch slurm/p4_vector_report.sh`; `sbatch slurm/p4_inspect_node.sh`; `sbatch slurm/p4_cache.sh` | 33970; 33926; 33931 |

⚠ The tiling runs were spread over haskell (34000–34017) and julia (35003–35012); compare tile
sizes only within one node, or rerun them on one node.

### OpenACC GPU and benchmarking experiments (P5)

GPU jobs use the Ada GPU (`rtx6000ada`, node haskell) unless noted; CPU comparison jobs run
on fortran (`-w fortran`). Every job compiles inside the job and writes its git commit, node
and settings at the top of its log in `results/logs/<name>_<jobid>.out`. Account limits that
shape these experiments: 1 GPU, 64 CPUs (= 32 MPI ranks with whole cores), 8 jobs per user.
GPU performance counters are blocked for users, so the two Nsight Compute jobs (28686, 28699)
fail with `ERR_NVGPUCTRPERM`; their logs are kept as evidence.

Correctness:

```bash
srun -p gpu --gres=gpu:1 -n1 -c2 --mem=8G -t 00:10:00 bash -c 'source env/modules.sh gpu && make openacc && make check'
```

Job 32634 on merged main (2d5b7c3): OpenACC vs serial PASS, max difference 1.3e-05 (tolerance
2.6e-04). Remove `build/heat_mpi` first: in the GPU environment `make check` would try to run it
with nvhpc's MPI, which fails.

| Experiment | Command | Jobs, tag in `results/runs.csv` |
|---|---|---|
| Data residency: naive (copy every step) vs resident, 2048² and 4096², 2000 steps | `sbatch --export=ALL,NX=2048,TAG=gpu_resident slurm/gpu.sh` and `sbatch --export=ALL,NX=2048,TAG=gpu_naive,EXTRA="--acc-mode naive" slurm/gpu.sh` (same with `NX=4096`) | 28412–28415, `gpu_resident`, `gpu_naive` |
| GPU size sweep 256²–8192², same work as P1's `sizes` | `sbatch slurm/sweep_gpu.sh` | 28704, `gpu_sizes` |
| Same sweep again (baseline for `--bc-once`) | `sbatch --export=ALL,TAG=gpu_sizes_r2 slurm/sweep_gpu.sh` | 31359, `gpu_sizes_r2` |
| Boundary filled once (1 kernel launch per step instead of 5) | `sbatch --export=ALL,TAG=gpu_sizes_bconce,EXTRA=--bc-once slurm/sweep_gpu.sh` | 31360, `gpu_sizes_bconce` |
| Nsight Systems timelines, 2048² and 4096², naive and resident | `sbatch --export=ALL,NX=2048 slurm/profile_gpu.sh`, `sbatch --export=ALL,NX=2048,ACC_MODE=naive slurm/profile_gpu.sh` (same with `NX=4096`) | 28684, 28685, 28697, 28698 → `results/profiles/nsys_*_stats.txt` |
| L2 test: same profiles with the L2 cache flushed before every step | `sbatch --export=ALL,NX=4096,ACC_MODE=naive,EXTRA="--flush-l2 256" slurm/profile_gpu.sh` (and `ACC_MODE=resident`) | 31361, 31362 |
| L2 test: grid much larger than L2 | `sbatch --export=ALL,NX=8192,ACC_MODE=naive slurm/profile_gpu.sh` (and `ACC_MODE=resident`) | 31363, 31364 |
| MPI-IO vs gather, interleaved, 1×16 and 2×16 ranks, 4096² and 8192², 30 pairs | `sbatch --export=ALL,TAG=ioalt slurm/io_mpi.sh` | 31409, `ioalt_gather`, `ioalt_mpiio` |
| Independent replication, 4096² | `sbatch --export=ALL,SIZES=4096,TAG=iorep slurm/io_mpi.sh` | 31411, `iorep_*` |
| Pre-registered confirmation, 4096², 2 nodes, 60 pairs | `sbatch --export=ALL,SIZES=4096,CONFIGS=2:32,REPS=60,TAG=io2n slurm/io_mpi.sh` | 31413, `io2n_*` |
| CPU side of CPU vs GPU (MPI 1–32 ranks, same work as the GPU sweep, rank placement logged) | `sbatch -w fortran --ntasks=32 --export=ALL,NX=2048,STEPS=953,REPS=5,RANKS="1 2 4 8 16 32",TAG=cpu32_bind slurm/scaling_mpi.sh` (and `NX=4096,STEPS=238`; `NX=8192,STEPS=59`) | 31429, 31428, 32605, `cpu32_bind` |
| Chiplet test: Slurm's placement vs ranks spread over all chiplets, alternated | `sbatch -w fortran slurm/l3_spread_test.sh` and `sbatch -w fortran --export=ALL,NX=8192,STEPS=59 slurm/l3_spread_test.sh` | 32611, 32612, `l3spread_default`, `l3spread_spread` |

The node topology used by the chiplet analysis was saved with
`srun -p cpu -w fortran -n1 lscpu -e=CPU,CORE,SOCKET,NODE,CACHE > results/fortran_topology.txt`.
Job accounting for all of these: `results/sacct_p5.txt` (`sacct -P -j <jobs> --format=JobID,JobName,State,Elapsed,MaxRSS,TotalCPU,NodeList,AllocTRES%60`).

Tables and figures (on any machine with `env/requirements-analysis.txt`):

```bash
python3 analysis/plots.py compare --tags gpu_naive gpu_resident --nx 2048 --labels "naive (copy every step)" "resident (copy once)" --metric time_total --out results/plots/p5_residency_2048.png
python3 analysis/plots.py compare --tags gpu_naive gpu_resident --nx 4096 --labels "naive (copy every step)" "resident (copy once)" --metric time_total --out results/plots/p5_residency_4096.png
python3 analysis/plots.py breakdown --tags gpu_resident --labels resident --out results/plots/p5_resident_breakdown.png
python3 analysis/plots.py sensitivity --tags sizes gpu_sizes --labels "serial, 1 core (reference kernel)" "GPU, OpenACC resident" --logx --logy --title "Time per cell update vs grid size" --out results/plots/p5_sizes_cpu_gpu.png
python3 analysis/plots.py sensitivity --tags gpu_sizes_r2 gpu_sizes_bconce --labels "GPU resident (5 launches/step)" "GPU resident + --bc-once (1 launch/step)" --logx --logy --title "Second GPU optimization: boundary filled once" --out results/plots/p5_bconce.png
python3 analysis/p5_tables.py all     # p5_cpu_vs_gpu.md/.png, p5_l2_test.md, p5_l3_spread.md
python3 analysis/p5_io_stats.py --tag ioalt --job 31409 --out results/plots/p5_io_interleaved.md
python3 analysis/p5_io_stats.py --tag iorep --out results/plots/p5_io_replication.md
python3 analysis/p5_io_stats.py --tag io2n --out results/plots/p5_io_2nodes_confirm.md
for j in 31429 31428 32605; do python3 analysis/p5_l3_placement.py --log results/logs/scaling_mpi_$j.out --topology results/fortran_topology.txt --tag cpu32_bind --job $j --out results/plots/p5_l3_placement_$j.md; done
```
