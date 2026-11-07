#!/bin/bash
# P1 baseline: serial solver, 3 grid sizes x 3 repeats, 2000 steps.
# Submit from the repo root:   sbatch slurm/p1_baseline.sh
# Other tag (e.g. after a kernel change): sbatch --export=ALL,TAG=baseline_avx512 slurm/p1_baseline.sh
# (P5 owns the shared templates; this is P1's own script.)
#SBATCH --job-name=p1_baseline
#SBATCH --partition=cpu
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
TAG=${TAG:-baseline}
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  $(git status --porcelain | wc -l) uncommitted file(s)"
lscpu | grep -E "Model name|^CPU\(s\)|L1d|L2|L3"
gcc --version | head -1

# Compile here, on the compute node (-march=native must see this CPU).
BUILD=build/$SLURM_JOB_ID
make serial BUILD=$BUILD
BIN=$BUILD/heat_serial

for n in 1024 2048 4096; do
  for rep in 1 2 3; do
    cmd="$BIN --nx $n --ny $n --steps 2000 --city synthetic --seed 42 --timing results/runs.csv --tag $TAG"
    echo "=== $cmd   (repeat $rep)"
    $cmd
  done
done
echo "=== done $(date -Is)"
