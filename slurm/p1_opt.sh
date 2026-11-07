#!/bin/bash
# P1 optimization, part 2: kernel variants (src/opt/bench_kernels.c), 3 repeats each,
# at a cache-resident size (512, about 4e9 updates) and the baseline size (2048, 2000 steps).
# Every variant is checked against the reference kernel inside the program.
# If perf exists on the node, also records hardware counters for ref vs permute.
#   sbatch slurm/p1_opt.sh
#SBATCH --job-name=p1_opt
#SBATCH --partition=cpu
#SBATCH --nodelist=fortran
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -uo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)"
lscpu | grep -E "Model name"

BUILD=build/$SLURM_JOB_ID
make serial bench BUILD=$BUILD || exit 1
BIN=$BUILD/bench_kernels

status=0
for cfg in "512 15258" "2048 2000"; do
  set -- $cfg
  echo "=== n=$1 steps=$2"
  $BIN --nx $1 --steps $2 --reps 3 --seed 42 --timing results/runs.csv || status=1
done

# Hardware counters (only if perf is installed on this node)
if command -v perf >/dev/null 2>&1; then
  for v in ref permute; do
    echo "=== perf stat, variant $v, n=2048 steps=500"
    perf stat -e cycles,instructions,cache-misses,LLC-load-misses \
      $BIN --nx 2048 --steps 500 --reps 1 --nocheck --variants $v 2>&1 || echo "(perf stat failed)"
  done
else
  echo "=== perf not available on $(hostname); hardware counters skipped"
fi

echo "=== done $(date -Is)"
exit $status
