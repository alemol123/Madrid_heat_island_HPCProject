#!/bin/bash
# Shared template (P5): hybrid MPI + OpenMP run of the heat solver.
# Submit from the repo root, e.g.
#   sbatch slurm/hybrid.sh
#   sbatch --nodes=2 --ntasks-per-node=4 --cpus-per-task=16 slurm/hybrid.sh
#   sbatch --ntasks-per-node=8 --cpus-per-task=4 --export=ALL,NX=4096,TAG=mytest slurm/hybrid.sh
#SBATCH --job-name=hybrid
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=8
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

# OpenMP: one thread per allocated core, pinned
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_PROC_BIND=${OMP_PROC_BIND:-close}
export OMP_PLACES=${OMP_PLACES:-cores}

# Settings: defaults below, override with --export=ALL,NAME=value
NX=${NX:-2048}
NY=${NY:-$NX}
STEPS=${STEPS:-2000}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
TAG=${TAG:-hybrid}
REPS=${REPS:-3}
EXTRA=${EXTRA:-}           # version-specific options, e.g. "--overlap" or "--decomp blocks"

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== nodes=$SLURM_NNODES ranks=$SLURM_NTASKS ranks/node=${SLURM_NTASKS_PER_NODE:-n/a} threads/rank=$OMP_NUM_THREADS"
echo "=== OMP_PROC_BIND=$OMP_PROC_BIND OMP_PLACES=$OMP_PLACES"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED tag=$TAG reps=$REPS extra=$EXTRA"
lscpu | grep -E "Model name|^CPU\(s\)" || true

# Compile on the compute node (-march=native must see this CPU)
BUILD=build/$SLURM_JOB_ID
make hybrid BUILD=$BUILD
[ -x "$BUILD/heat_hybrid" ] || { echo "=== ERROR: heat_hybrid was not built (is src/hybrid/heat_hybrid.c there yet?)"; exit 1; }

# srun needs --cpus-per-task repeated: newer Slurm does not pass it on from sbatch
CMD="srun --mpi=pmix --cpus-per-task=$SLURM_CPUS_PER_TASK --cpu-bind=cores $BUILD/heat_hybrid --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --out $BUILD/field.bin --timing results/runs.csv --tag $TAG $EXTRA"
echo "=== command: $CMD"

for rep in $(seq 1 "$REPS"); do
  echo "=== repeat $rep"
  $CMD
done

echo "=== done $(date -Is)"