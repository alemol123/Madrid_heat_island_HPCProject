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

The reference file in `tests/ref/` was made with:

```bash
./build/heat_serial --nx 256 --ny 256 --steps 200 --city synthetic --seed 42 \
    --out tests/ref/ref_seed42_256x256_200.bin
```

## 4. Other versions

(P2, P3, P4, P5: add your commands here.)
