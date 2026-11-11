#!/bin/bash
# MPI master-worker ensemble: runs every job of a job list with tools/run_ensemble.c.
# Run  sbatch slurm/ens_build.sh  first (once). Then, from the repo root, e.g.
#   sbatch slurm/ensemble.sh                                       # the science sweep, dynamic, 1 node, 8 workers
#   sbatch --ntasks-per-node=65 --export=ALL,JOBS=data/sweep/jobs_skewed.txt,MODE=dynamic,TAG=skew_dyn_64,NOFIELDS=1,OUT=results/ens_bench/skew_dyn_64 slurm/ensemble.sh
#   sbatch --nodes=2 --ntasks-per-node=33 ...                      # 2 nodes
# Rank 0 only hands out jobs, so  workers = tasks - 1.
#SBATCH --job-name=ensemble
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=9
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=1G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu
export OMP_NUM_THREADS=1

# Settings: defaults below, override with --export=ALL,NAME=value
JOBS=${JOBS:-data/sweep/jobs_science.txt}
MODE=${MODE:-dynamic}                  # static | cyclic | dynamic
TAG=${TAG:-ens_${MODE}}
OUT=${OUT:-results/ens_$SLURM_JOB_ID}
RESULT_CSV=${RESULT_CSV:-results/ensemble.csv}
NOFIELDS=${NOFIELDS:-0}                # 1 = do not keep the temperature fields (benchmarks)
EXTRA=""
[ "$NOFIELDS" = "1" ] && EXTRA="--no-fields"

for f in build/ens/heat_serial build/ens/run_ensemble "$JOBS"; do
  [ -e "$f" ] || { echo "=== ERROR: $f is missing (run: sbatch slurm/ens_build.sh)"; exit 1; }
done

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== nodes=$SLURM_NNODES tasks=$SLURM_NTASKS (workers=$((SLURM_NTASKS - 1)))"
echo "=== git commit: $(git rev-parse --short HEAD)"
echo "=== jobs=$JOBS ($(grep -cv '^[[:space:]]*\(#\|$\)' "$JOBS") jobs) mode=$MODE tag=$TAG out=$OUT"

srun --mpi=pmix build/ens/run_ensemble --jobs "$JOBS" --solver build/ens/heat_serial --mode "$MODE" \
     --outdir "$OUT" --result-csv "$RESULT_CSV" --tag "$TAG" $EXTRA

echo "=== done $(date -Is)"
