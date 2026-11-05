#!/bin/bash
# Shared template (P5): OpenACC (GPU) run of the heat solver.
# Submit from the repo root, e.g.
#   sbatch slurm/gpu.sh                                   (Ada GPU, default)
#   sbatch --gres=gpu:rtx6000bb:1 slurm/gpu.sh            (any Blackwell GPU)
#   sbatch --export=ALL,NX=8192,TAG=mytest,ACC_TIME=1 slurm/gpu.sh
# Keep one GPU type for a set of measurements (see env/README.md).
#SBATCH --job-name=gpu
#SBATCH --partition=gpu
#SBATCH --gres=gpu:rtx6000ada:1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh gpu
export OMP_NUM_THREADS=1

# Settings: defaults below, override with --export=ALL,NAME=value
NX=${NX:-2048}
NY=${NY:-$NX}
STEPS=${STEPS:-2000}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
TAG=${TAG:-openacc}
REPS=${REPS:-3}
EXTRA=${EXTRA:-}           # version-specific options
ACC_TIME=${ACC_TIME:-0}    # 1 = print OpenACC kernel and transfer times
if [ "$ACC_TIME" = "1" ]; then export NVCOMPILER_ACC_TIME=1; fi

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED tag=$TAG reps=$REPS extra=$EXTRA acc_time=$ACC_TIME"
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
nvc --version | head -2

# Compile on the GPU node (ccnative must see this GPU)
BUILD=build/$SLURM_JOB_ID
make openacc BUILD=$BUILD
[ -x "$BUILD/heat_openacc" ] || { echo "=== ERROR: heat_openacc was not built (is src/openacc/heat_openacc.c there yet?)"; exit 1; }

CMD="$BUILD/heat_openacc --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --out $BUILD/field.bin --timing results/runs.csv --tag $TAG $EXTRA"
echo "=== command: $CMD"

for rep in $(seq 1 "$REPS"); do
  echo "=== repeat $rep"
  $CMD
done

echo "=== done $(date -Is)"