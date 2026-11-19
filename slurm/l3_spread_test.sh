#!/bin/bash
# P5: causal test of the chiplet (L3 + memory path) explanation of the MPI scaling jump.
# Same allocation, same number of ranks, two placements, alternated in every repeat:
#   default: Slurm's own placement (2..16 ranks end up on only 2 chiplets)
#   spread:  the same ranks spread evenly over ALL chiplets of the allocation
# If memory paths and L3 per chiplet are the bottleneck, "spread" must be clearly faster.
# Needs the node's topology: results/<node>_topology.txt (lscpu -e=CPU,CORE,SOCKET,NODE,CACHE).
#   sbatch -w fortran slurm/l3_spread_test.sh
#   sbatch -w fortran --export=ALL,NX=8192,STEPS=59 slurm/l3_spread_test.sh
#SBATCH --job-name=l3_spread
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=32
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:20:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu
export OMP_NUM_THREADS=1

NX=${NX:-4096}
STEPS=${STEPS:-238}
RANKS=${RANKS:-"8 16"}
REPS=${REPS:-5}
TAG=${TAG:-l3spread}
CSV=${CSV:-results/runs.csv}
TOPO=${TOPO:-results/$(hostname -s)_topology.txt}

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
echo "=== nx=$NX steps=$STEPS ranks='$RANKS' reps=$REPS tag=$TAG topology=$TOPO"
[ -f "$TOPO" ] || { echo "=== ERROR: $TOPO not found"; exit 1; }

BUILD=build/$SLURM_JOB_ID
make mpi BUILD=$BUILD
[ -x "$BUILD/heat_mpi" ] || { echo "=== ERROR: heat_mpi was not built"; exit 1; }

ALLOWED=$(awk '/Cpus_allowed_list/ {print $2}' /proc/self/status)
echo "=== CPUs of this allocation: $ALLOWED"

for r in $RANKS; do
  SPREAD=$(python3 analysis/p5_spread_cpus.py --topology "$TOPO" --allowed "$ALLOWED" --ranks "$r")
  echo "=== ranks=$r spread CPUs: $SPREAD"
  C=$(( SLURM_NTASKS / r ))   # give the spread step the whole allocation, so every chosen CPU is inside it
  for rep in $(seq 1 "$REPS"); do
    for place in default spread; do
      if [ "$place" = default ]; then
        CMD="srun --mpi=pmix -n $r --cpus-per-task=1 --cpu-bind=verbose,cores"
      else
        CMD="srun --mpi=pmix -n $r --cpus-per-task=$C --cpu-bind=verbose,map_cpu:$SPREAD"
      fi
      CMD="$CMD $BUILD/heat_mpi --nx $NX --ny $NX --steps $STEPS --city synthetic --seed 42 --timing $CSV --tag ${TAG}_$place"
      echo "=== ranks=$r repeat $rep placement=$place  command: $CMD"
      $CMD
    done
  done
done

echo "=== done $(date -Is)"
