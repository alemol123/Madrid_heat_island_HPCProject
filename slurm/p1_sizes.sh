#!/bin/bash
# P1 experiment: problem-size sensitivity, serial, 256 .. 8192, 3 repeats.
# Steps are chosen so each run does about 4e9 cell updates (a few seconds),
# so the time per cell-update is comparable across sizes.
#   sbatch slurm/p1_sizes.sh
# Plot ns per cell-update (time_compute / (nx*ny*steps)) against nx; the jumps
# come where the two float buffers stop fitting in L2, then L3 (see lscpu in the log).
#SBATCH --job-name=p1_sizes
#SBATCH --partition=cpu
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=4G
#SBATCH --time=00:45:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)"
lscpu | grep -E "Model name|L1d|L2|L3"

BUILD=build/$SLURM_JOB_ID
make serial BUILD=$BUILD
BIN=$BUILD/heat_serial

TARGET=4000000000
for n in 256 362 512 724 1024 1448 2048 2896 4096 5792 8192; do
  steps=$(( TARGET / (n * n) ))
  for rep in 1 2 3; do
    echo "=== n=$n steps=$steps repeat $rep  (two buffers = $(( 2 * (n + 2) * (n + 2) * 4 / 1024 )) KiB)"
    $BIN --nx $n --ny $n --steps $steps --city synthetic --seed 42 --timing results/runs.csv --tag sizes
  done
done
echo "=== done $(date -Is)"
