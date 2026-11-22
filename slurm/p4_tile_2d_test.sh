#!/bin/bash
#SBATCH --job-name=p4-tile-2d
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:15:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

VARIANT=${VARIANT:-tiled-2d}
NX=${NX:-4096}
NY=${NY:-$NX}
STEPS=${STEPS:-1000}
REPS=${REPS:-3}
TILE_X=${TILE_X:-256}
TILE_Y=${TILE_Y:-32}
TAG="p4-tile-${VARIANT}-${TILE_X}x${TILE_Y}"

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_PROC_BIND=close
export OMP_PLACES=cores

BUILD="build/p4-tile-${VARIANT}-${SLURM_JOB_ID}"
FLAGS="-O3 -march=native -std=c11 -Wall -Wextra"

if [ "$VARIANT" = "tiled-2d" ]; then
    FLAGS="$FLAGS -DSTEP_TILED_2D -DSTEP_TILE_X=$TILE_X -DSTEP_TILE_Y=$TILE_Y"
elif [ "$VARIANT" != "baseline" ]; then
    echo "Unknown VARIANT: $VARIANT"
    exit 1
fi

echo "=== variant=$VARIANT job=$SLURM_JOB_ID node=$SLURM_JOB_NODELIST"
echo "=== nx=$NX ny=$NY steps=$STEPS threads=$OMP_NUM_THREADS"
echo "=== tile_x=$TILE_X tile_y=$TILE_Y repetitions=$REPS"
echo "=== compiler flags: $FLAGS"

make hybrid BUILD="$BUILD" CFLAGS="$FLAGS"

for rep in $(seq 1 "$REPS"); do
    echo "=== repeat $rep"
    srun --mpi=pmix --cpus-per-task="$SLURM_CPUS_PER_TASK" \
        --cpu-bind=cores "$BUILD/heat_hybrid" \
        --nx "$NX" --ny "$NY" --steps "$STEPS" \
        --city synthetic --seed 42 \
        --out "$BUILD/field-${rep}.bin" \
        --timing results/runs.csv --tag "$TAG"
done

echo "=== completed variant=$VARIANT"
