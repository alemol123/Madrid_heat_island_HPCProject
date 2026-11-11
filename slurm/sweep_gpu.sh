#!/bin/bash
# Shared template (P5): GPU problem-size sweep in ONE allocation (one build, all sizes back to back
# on the same GPU, so the points are comparable). Uses 1 job slot instead of one per size.
# Submit from the repo root, e.g.
#   sbatch slurm/sweep_gpu.sh                                         (resident, Ale's serial sizes/steps)
#   sbatch --export=ALL,EXTRA="--acc-mode naive",TAG=gpu_sizes_naive slurm/sweep_gpu.sh
# SIZES is a list of NX:STEPS pairs (square grids). Default = the same sizes and steps as P1's serial
# size sweep (about 4e9 cell updates each), so every GPU point has a serial point with identical work.
# WRITE=0 (default) does not write the field, like P1's size sweep, so time_io and time_total compare
# like for like. WRITE=1 also writes it (adds the file write to time_io).
#SBATCH --job-name=sweep_gpu
#SBATCH --partition=gpu
#SBATCH --gres=gpu:rtx6000ada:1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=16G
#SBATCH --time=01:00:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh gpu
export OMP_NUM_THREADS=1

# Settings: defaults below, override with --export=ALL,NAME=value
SIZES=${SIZES:-"256:61035 512:15258 1024:3814 2048:953 2896:476 4096:238 5792:119 8192:59"}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
TAG=${TAG:-gpu_sizes}
REPS=${REPS:-3}
CSV=${CSV:-results/runs.csv}
WRITE=${WRITE:-0}
EXTRA=${EXTRA:-}

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== sizes=\"$SIZES\" city=$CITY seed=$SEED tag=$TAG reps=$REPS csv=$CSV write=$WRITE extra=$EXTRA"
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
nvc --version | head -2

# Compile once on the GPU node (ccnative must see this GPU)
BUILD=build/$SLURM_JOB_ID
make openacc BUILD=$BUILD
[ -x "$BUILD/heat_openacc" ] || { echo "=== ERROR: heat_openacc was not built"; exit 1; }

OUT=""
if [ "$WRITE" = "1" ]; then OUT="--out $BUILD/field.bin"; fi

for p in $SIZES; do
  n=${p%:*}; s=${p#*:}
  CMD="$BUILD/heat_openacc --nx $n --ny $n --steps $s --city $CITY --seed $SEED $OUT --timing $CSV --tag $TAG $EXTRA"
  echo "=== nx=$n steps=$s  command: $CMD"
  for rep in $(seq 1 "$REPS"); do
    echo "=== repeat $rep"
    $CMD
  done
done

echo "=== done $(date -Is)"