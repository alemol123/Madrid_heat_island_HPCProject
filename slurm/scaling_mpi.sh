#!/bin/bash
# Shared template (P5): MPI strong-scaling sweep in ONE allocation.
# All rank counts run back to back on the same node(s), so the points are comparable.
# Submit from the repo root, e.g.
#   sbatch slurm/scaling_mpi.sh                                     (1 node, 1..64 ranks)
#   sbatch --nodes=2 --ntasks-per-node=32 slurm/scaling_mpi.sh      (same sweep over 2 nodes)
#   sbatch --export=ALL,NX=8192,RANKS="1 4 16 64",TAG=mpi_strong_8192 slurm/scaling_mpi.sh
#   sbatch --export=ALL,TAG=mpi_strong_blocks,EXTRA="--decomp blocks" slurm/scaling_mpi.sh
# Each rank count runs REPS times; rank counts larger than the allocation are skipped.
# No --out: scaling runs only need timings, not the temperature field.
#SBATCH --job-name=scaling_mpi
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=64
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:45:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu
export OMP_NUM_THREADS=1   # pure MPI: one thread per rank

# Settings: defaults below, override with --export=ALL,NAME=value
NX=${NX:-4096}
NY=${NY:-$NX}
STEPS=${STEPS:-2000}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
TAG=${TAG:-mpi_strong}
REPS=${REPS:-3}
RANKS=${RANKS:-"1 2 4 8 16 32 64"}
CSV=${CSV:-results/runs.csv}
EXTRA=${EXTRA:-}           # version-specific options, e.g. "--overlap" or "--decomp blocks"

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== allocation: nodes=$SLURM_NNODES tasks=$SLURM_NTASKS"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED tag=$TAG reps=$REPS ranks='$RANKS' csv=$CSV extra=$EXTRA"
lscpu | grep -E "Model name|^CPU\(s\)" || true

# Compile once, on the compute node (-march=native must see this CPU)
BUILD=build/$SLURM_JOB_ID
make mpi BUILD=$BUILD
[ -x "$BUILD/heat_mpi" ] || { echo "=== ERROR: heat_mpi was not built (is src/mpi/heat_mpi.c there yet?)"; exit 1; }

for r in $RANKS; do
  if [ "$r" -gt "$SLURM_NTASKS" ]; then
    echo "=== skip $r ranks (allocation has only $SLURM_NTASKS tasks)"
    continue
  fi
  CMD="srun --mpi=pmix -n $r --cpus-per-task=1 --cpu-bind=cores $BUILD/heat_mpi --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --timing $CSV --tag $TAG $EXTRA"
  echo "=== ranks=$r  command: $CMD"
  for rep in $(seq 1 "$REPS"); do
    echo "=== ranks=$r repeat $rep"
    $CMD
  done
done

echo "=== done $(date -Is)"