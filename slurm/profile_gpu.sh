#!/bin/bash
# Shared template (P5): profile a GPU run with Nsight Systems (nsys) or Nsight Compute (ncu).
# Submit from the repo root, e.g.
#   sbatch slurm/profile_gpu.sh                                        (nsys, resident, 2048^2)
#   sbatch --export=ALL,ACC_MODE=naive slurm/profile_gpu.sh            (nsys, naive)
#   sbatch --export=ALL,TOOL=ncu slurm/profile_gpu.sh                  (ncu, all kernels in detail)
#   sbatch --export=ALL,CMD="path/to/other_binary --args" slurm/profile_gpu.sh   (profile any program)
# nsys = whole-run timeline (kernels, copies, gaps, setup). ncu = each kernel in detail
# (memory throughput, occupancy, bottlenecks).
# Profiled runs are slower than normal runs, so they do NOT write to results/runs.csv.
# Big report files (.nsys-rep, .ncu-rep, .sqlite) stay out of git; the text summaries are committed.
#SBATCH --job-name=profile
#SBATCH --partition=gpu
#SBATCH --gres=gpu:rtx6000ada:1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh gpu
export OMP_NUM_THREADS=1

# Settings: defaults below, override with --export=ALL,NAME=value
TOOL=${TOOL:-nsys}            # nsys or ncu
ACC_MODE=${ACC_MODE:-resident}
NX=${NX:-2048}
NY=${NY:-$NX}
STEPS=${STEPS:-200}           # a few hundred steps show the pattern; profiling slows the run
CITY=${CITY:-synthetic}
SEED=${SEED:-42}
KERNEL=${KERNEL:-.}           # ncu only: kernel name regex ("." = all kernels)
EXTRA=${EXTRA:-}
CMD=${CMD:-}                  # optional: profile another program instead of heat_openacc
OUTDIR=results/profiles
mkdir -p "$OUTDIR"

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== tool=$TOOL acc_mode=$ACC_MODE nx=$NX ny=$NY steps=$STEPS city=$CITY seed=$SEED kernel=$KERNEL extra=$EXTRA"
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
nvc --version | head -2
command -v "$TOOL" >/dev/null || { echo "=== ERROR: $TOOL not found on PATH after loading the gpu environment"; exit 1; }
$TOOL --version | head -1

if [ -z "$CMD" ]; then
  # Compile on the GPU node (ccnative must see this GPU)
  BUILD=build/$SLURM_JOB_ID
  make openacc BUILD=$BUILD
  [ -x "$BUILD/heat_openacc" ] || { echo "=== ERROR: heat_openacc was not built"; exit 1; }
  # No --timing and no --out: profiled timings are not valid results
  CMD="$BUILD/heat_openacc --nx $NX --ny $NY --steps $STEPS --city $CITY --seed $SEED --acc-mode $ACC_MODE $EXTRA"
  NAME="${TOOL}_${ACC_MODE}_${NX}_${SLURM_JOB_ID}"
else
  NAME="${TOOL}_custom_${SLURM_JOB_ID}"
fi
echo "=== command: $CMD"

case "$TOOL" in
  nsys)
    echo "=== nsys profile -> $OUTDIR/$NAME.nsys-rep"
    nsys profile --trace=cuda,openacc --stats=true --force-overwrite=true \
         -o "$OUTDIR/$NAME" $CMD | tee "$OUTDIR/${NAME}_stats.txt" ;;
  ncu)
    # Skip warm-up launches, then profile 10 launches (= 2 full steps: stencil + 4 boundary kernels each)
    echo "=== ncu profile of kernels '$KERNEL' -> $OUTDIR/$NAME.ncu-rep"
    ncu --kernel-name "regex:$KERNEL" --launch-skip 50 --launch-count 10 \
        --set full --force-overwrite -o "$OUTDIR/$NAME" $CMD
    ncu --import "$OUTDIR/$NAME.ncu-rep" --page details | tee "$OUTDIR/${NAME}_stats.txt" ;;
  *)
    echo "=== ERROR: TOOL must be nsys or ncu"; exit 1 ;;
esac

echo "=== done $(date -Is)"