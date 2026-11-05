#!/bin/bash
# Shared template (P5): MPI run of the heat solver.
# Submit from the repo root, e.g.
#   sbatch slurm/mpi.sh
#   sbatch --nodes=2 --ntasks-per-node=8 slurm/mpi.sh
#   sbatch --nodes=1 --ntasks-per-node=4 --export=ALL,NX=4096,TAG=mytest,EXTRA="--overlap" slurm/mpi.sh
#SBATCH --job-name=mpi
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu
export OMP_NUM_THREADS=1   # pure MPI: one thread per rank

# Settings: defaults below, override with --export=ALL,NAME=value
NX=${NX:-2048}
NY=${NY:-$NX}
STEPS=${STEPS:-2000}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
TAG=${TAG:-mpi}
REPS=${REPS:-3}
EXTRA=${EXTRA:-}           # version-specific options, e.g. "--overlap" or "--decomp blocks"

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== nodes=$SLURM_NNODES ranks=$SLURM_NTASKS ranks/node=$SLURM_NTASKS_PER_NODE"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain | wc -l) uncommitted files)"
echo "=== nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED tag=$TAG reps=$REPS extra=$EXTRA"
lscpu | grep -E "Model name|^CPU\(s\)" || true

# Compile on the compute node (-march=native must see this CPU)
BUILD=build/$SLURM_JOB_ID
make mpi BUILD=$BUILD
[ -x "$BUILD/heat_mpi" ] || { echo "=== ERROR: heat_mpi was not built (is src/mpi/heat_mpi.c there yet?)"; exit 1; }

CMD="srun --mpi=pmix $BUILD/heat_mpi --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --out $BUILD/field.bin --timing results/runs.csv --tag $TAG $EXTRA"
echo "=== command: $CMD"

for rep in $(seq 1 "$REPS"); do
  echo "=== repeat $rep"
  $CMD
done

echo "=== done $(date -Is)"