#!/bin/bash
# Shared template (P5): hybrid MPI + OpenMP scaling sweeps in ONE allocation.
# MODE=threads: 1 rank, OpenMP threads 1..64            (tag TAG_THREADS)
# MODE=mix:     fixed CORES, ranks x threads pairs      (tag TAG_MIX), e.g. 64x1 ... 1x64
# MODE=both:    both sweeps (default)
# Submit from the repo root, e.g.
#   sbatch slurm/scaling_hybrid.sh
#   sbatch --export=ALL,MODE=mix,PAIRS="8x1 4x2 2x4 1x8",TAG_MIX=hybrid_mix8 slurm/scaling_hybrid.sh
#   sbatch --nodes=2 --ntasks-per-node=64 --export=ALL,MODE=mix,PAIRS="128x1 32x4 8x16" slurm/scaling_hybrid.sh
# Threads are pinned (OMP_PROC_BIND=close, OMP_PLACES=cores). Each point runs REPS times.
#SBATCH --job-name=scaling_hybrid
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
export OMP_PROC_BIND=${OMP_PROC_BIND:-close}
export OMP_PLACES=${OMP_PLACES:-cores}

# Settings: defaults below, override with --export=ALL,NAME=value
NX=${NX:-4096}
NY=${NY:-$NX}
STEPS=${STEPS:-2000}
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
REPS=${REPS:-3}
MODE=${MODE:-both}                                   # threads | mix | both
THREADS=${THREADS:-"1 2 4 8 16 32 64"}
PAIRS=${PAIRS:-"64x1 32x2 16x4 8x8 4x16 2x32 1x64"} # ranks x threads
TAG_THREADS=${TAG_THREADS:-hybrid_threads}
TAG_MIX=${TAG_MIX:-hybrid_mix}
CSV=${CSV:-results/runs.csv}
EXTRA=${EXTRA:-}

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== allocation: nodes=$SLURM_NNODES tasks=$SLURM_NTASKS  OMP_PROC_BIND=$OMP_PROC_BIND OMP_PLACES=$OMP_PLACES"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED reps=$REPS mode=$MODE csv=$CSV extra=$EXTRA"
lscpu | grep -E "Model name|^CPU\(s\)" || true

BUILD=build/$SLURM_JOB_ID
make hybrid BUILD=$BUILD
[ -x "$BUILD/heat_hybrid" ] || { echo "=== ERROR: heat_hybrid was not built (is src/hybrid/heat_hybrid.c there yet?)"; exit 1; }

run_point () {   # ranks threads tag
  local r=$1 t=$2 tag=$3
  if [ $(( r * t )) -gt "$SLURM_NTASKS" ]; then
    echo "=== skip ${r}x${t} (needs $(( r * t )) cores, allocation has $SLURM_NTASKS)"
    return
  fi
  export OMP_NUM_THREADS=$t
  local CMD="srun --mpi=pmix -n $r --cpus-per-task=$t --cpu-bind=cores $BUILD/heat_hybrid --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --timing $CSV --tag $tag $EXTRA"
  echo "=== ranks=$r threads=$t  command: OMP_NUM_THREADS=$t $CMD"
  for rep in $(seq 1 "$REPS"); do
    echo "=== ${r}x${t} repeat $rep"
    $CMD
  done
}

if [ "$MODE" = "threads" ] || [ "$MODE" = "both" ]; then
  for t in $THREADS; do run_point 1 "$t" "$TAG_THREADS"; done
fi
if [ "$MODE" = "mix" ] || [ "$MODE" = "both" ]; then
  for p in $PAIRS; do run_point "${p%x*}" "${p#*x}" "$TAG_MIX"; done
fi

echo "=== done $(date -Is)"