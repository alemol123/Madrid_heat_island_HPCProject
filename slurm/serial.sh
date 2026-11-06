#!/bin/bash
# Shared template (P5): serial run of the heat solver.
# Submit from the repo root, e.g.
#   sbatch slurm/serial.sh
#   sbatch --export=ALL,NX=4096,STEPS=2000,TAG=mytest,REPS=3 slurm/serial.sh
#SBATCH --job-name=serial
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

# Settings: defaults below, override with --export=ALL,NAME=value
NX=${NX:-2048}
NY=${NY:-$NX}
STEPS=${STEPS:-2000}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
TAG=${TAG:-serial}
REPS=${REPS:-3}

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED tag=$TAG reps=$REPS"
lscpu | grep -E "Model name|^CPU\(s\)" || true

# Compile on the compute node (-march=native must see this CPU)
BUILD=build/$SLURM_JOB_ID
make serial BUILD=$BUILD

CMD="$BUILD/heat_serial --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --out $BUILD/field.bin --timing results/runs.csv --tag $TAG"
echo "=== command: $CMD"

for rep in $(seq 1 "$REPS"); do
  echo "=== repeat $rep"
  $CMD
done

echo "=== done $(date -Is)"