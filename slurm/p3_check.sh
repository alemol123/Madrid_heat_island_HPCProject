#!/bin/bash
# P3: run "make check" on a compute node with 4 MPI tasks (MPI vs serial, 1 to 4 ranks).
# Submit from the repo root:
#   sbatch slurm/p3_check.sh
#SBATCH --job-name=p3_check
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu
export OMP_NUM_THREADS=1   # pure MPI: one thread per rank

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== nodes=$SLURM_NNODES ranks=$SLURM_NTASKS"
echo "=== git commit: $(git rev-parse --short HEAD)  ($(git status --porcelain -- . ':(exclude)results' | wc -l) uncommitted files outside results/)"
lscpu | grep -E "Model name|^CPU\(s\)" || true

# Compile on the compute node (-march=native must see this CPU).
# Plain "make mpi", not BUILD=build/$SLURM_JOB_ID: tests/check.sh looks in build/.
make mpi
MPIRUN="srun --mpi=pmix -n" make check

echo "=== done $(date -Is)"
