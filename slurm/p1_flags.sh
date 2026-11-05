#!/bin/bash
# P1 optimization, part 1: compiler flags (before/after), serial, 2048x2048, 3 repeats.
# Every variant must still pass compare.py against the -O2 output.
#   sbatch slurm/p1_flags.sh
# Part 2 (code changes: precomputed per-cell coefficients, float vs double,
# #pragma omp simd) is added when those variants exist.
#SBATCH --job-name=p1_flags
#SBATCH --partition=cpu
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)"
lscpu | grep -E "Model name|Flags" | cut -c1-200

N=2048; STEPS=2000
declare -A FLAGS=(
  [O2]="-O2 -std=c11"
  [O3]="-O3 -std=c11"
  [O3_native]="-O3 -march=native -std=c11"
  [O3_native_fma]="-O3 -march=native -std=c11 -ffp-contract=fast"
)
REFOUT=build/$SLURM_JOB_ID/O2/field.bin
for v in O2 O3 O3_native O3_native_fma; do
  B=build/$SLURM_JOB_ID/$v
  make serial BUILD=$B CFLAGS="${FLAGS[$v]} -fopt-info-vec-optimized=$B/vec.txt"
  echo "=== $v: ${FLAGS[$v]}   vectorized loops reported: $(grep -c 'vectorized' $B/vec.txt || true)"
  grep "step.c" $B/vec.txt || echo "    (no vectorized loop in step.c)"
  for rep in 1 2 3; do
    $B/heat_serial --nx $N --ny $N --steps $STEPS --city synthetic --seed 42 \
        --out $B/field.bin --timing results/runs.csv --tag "flags_$v"
  done
  python3 tools/compare.py "$REFOUT" "$B/field.bin"
done
echo "=== done $(date -Is)"
