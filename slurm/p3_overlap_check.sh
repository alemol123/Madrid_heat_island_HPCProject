#!/bin/bash
# P3: check that MPI, with and without --overlap, is bitwise identical to serial
# (tools/compare.py --exact) on 1, 2, 3 and 4 ranks.
# Stand-in for the MPI part of tests/check.sh until P1's PR #12 restores it.
#
# Cluster (from the repo root):  sbatch slurm/p3_overlap_check.sh
# Locally:                       bash slurm/p3_overlap_check.sh      (uses "mpirun -np")
#SBATCH --job-name=p3_overlap_check
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/%x_%j.out

set -uo pipefail
if [ -n "${SLURM_JOB_ID:-}" ]; then
  cd "$SLURM_SUBMIT_DIR"
  source env/modules.sh cpu
  MPIRUN=${MPIRUN:-"srun --mpi=pmix -n"}
  echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
  lscpu | grep -E "Model name|^CPU\(s\)" || true
else
  cd "$(dirname "$0")/.."
  MPIRUN=${MPIRUN:-"mpirun -np"}
fi
export OMP_NUM_THREADS=1   # pure MPI: one thread per rank
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- src | wc -l | tr -d ' ') uncommitted files in src/)"

# Compile here (on the cluster: on the compute node, -march=native must see this CPU).
make serial mpi || { echo "build failed"; exit 1; }

B=build
OUT=$B/p3_overlap_check
mkdir -p "$OUT"
fail=0

# nx ny steps seed name. 250x251 does not divide evenly by 2, 3 or 4 ranks.
# 64x6 gives strips of 1 and 2 rows (6 rows on 4 ranks = 2,2,1,1): the --overlap
# special cases for strips with no inner rows.
CASES=("256 256 200 42 s256" "250 251 120 7 odd" "64 6 50 3 tiny")

for case in "${CASES[@]}"; do
  set -- $case
  $B/heat_serial --nx $1 --ny $2 --steps $3 --city synthetic --seed $4 \
      --out $OUT/serial_$5.bin > $OUT/serial_$5.log 2>&1 \
    || { echo "serial $5 run failed (see $OUT/serial_$5.log)"; fail=1; }
done

for np in 1 2 3 4; do
  for case in "${CASES[@]}"; do
    set -- $case
    for mode in plain overlap; do
      flag=""; [ $mode = overlap ] && flag="--overlap"
      name=mpi_${np}_$5_$mode
      printf '%-28s ' "np=$np $5 $mode"
      if ! $MPIRUN $np $B/heat_mpi --nx $1 --ny $2 --steps $3 --city synthetic --seed $4 $flag \
             --out $OUT/$name.bin > $OUT/$name.log 2>&1; then
        echo "RUN FAILED (see $OUT/$name.log)"; fail=1; continue
      fi
      python3 tools/compare.py --exact $OUT/serial_$5.bin $OUT/$name.bin || fail=1
    done
  done
done

if [ $fail -eq 0 ]; then echo "p3_overlap_check: ALL PASSED"; else echo "p3_overlap_check: FAILED"; fi
exit $fail
