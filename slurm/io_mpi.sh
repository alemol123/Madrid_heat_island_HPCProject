#!/bin/bash
# Shared template (P5): output I/O of the MPI version, gather-to-rank-0 vs MPI-IO (--io gather|mpiio).
# One allocation of 2 nodes; every point runs on 1 node or on both nodes inside it, back to back.
# Submit from the repo root, e.g.
#   sbatch slurm/io_mpi.sh
#   sbatch --export=ALL,SIZES="2048 4096 8192",CONFIGS="1:16 1:64 2:128",REPS=3 slurm/io_mpi.sh
# CONFIGS is a list of NODES:RANKS. I/O does not depend on the number of steps, so STEPS is small.
# After each size and configuration the script checks that the two output files are byte-identical.
#SBATCH --job-name=io_mpi
#SBATCH --partition=cpu
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=64
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
SIZES=${SIZES:-"4096 8192"}
CONFIGS=${CONFIGS:-"1:64 2:128"}
STEPS=${STEPS:-10}
REPS=${REPS:-3}
TAG=${TAG:-io}
CSV=${CSV:-results/runs.csv}

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== sizes=\"$SIZES\" configs(nodes:ranks)=\"$CONFIGS\" steps=$STEPS reps=$REPS tag=$TAG csv=$CSV"

# Compile on the compute node
BUILD=build/$SLURM_JOB_ID
make mpi BUILD=$BUILD
[ -x "$BUILD/heat_mpi" ] || { echo "=== ERROR: heat_mpi was not built"; exit 1; }

# The file system matters for MPI-IO: record it
echo "=== output directory $BUILD is on file system type: $(stat -f -c %T "$BUILD")"
df -hT "$BUILD" | tail -1

for n in $SIZES; do
  for cfg in $CONFIGS; do
    nodes=${cfg%:*}; r=${cfg#*:}
    if [ "$r" -gt "$SLURM_NTASKS" ] || [ "$nodes" -gt "$SLURM_NNODES" ]; then
      echo "=== skip n=$n $cfg: does not fit the allocation"; continue
    fi
    for io in gather mpiio; do
      CMD="srun --mpi=pmix -N $nodes -n $r --cpus-per-task=1 --cpu-bind=cores $BUILD/heat_mpi --nx $n --ny $n --steps $STEPS --city synthetic --seed 42 --out $BUILD/field_$io.bin --io $io --timing $CSV --tag ${TAG}_$io"
      echo "=== n=$n nodes=$nodes ranks=$r io=$io  command: $CMD"
      for rep in $(seq 1 "$REPS"); do
        echo "=== repeat $rep"
        $CMD
      done
    done
    if cmp -s "$BUILD/field_gather.bin" "$BUILD/field_mpiio.bin"; then
      echo "=== CHECK n=$n nodes=$nodes ranks=$r: gather and mpiio files are byte-identical"
    else
      echo "=== CHECK FAILED n=$n nodes=$nodes ranks=$r: gather and mpiio files differ"; exit 1
    fi
    rm -f "$BUILD"/field_*.bin
  done
done

echo "=== done $(date -Is)"