#!/bin/bash
# P1: halo-exchange tests for the MPI version on the cluster, spread over 2 nodes so
# the ghost rows really travel over the network (1, 2, 3 and 5 ranks), plus the
# full "make check" (MPI vs serial, bitwise) in the same allocation.
#   sbatch slurm/p1_halo.sh
#SBATCH --job-name=p1_halo
#SBATCH --partition=cpu
#SBATCH --nodes=2
#SBATCH --ntasks=5
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem-per-cpu=1G
#SBATCH --time=00:15:00
#SBATCH --output=results/logs/%x_%j.out

set -uo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu
export OMP_NUM_THREADS=1

echo "=== job $SLURM_JOB_ID on $SLURM_JOB_NODELIST at $(date -Is)"
echo "=== nodes=$SLURM_NNODES ranks=$SLURM_NTASKS"
echo "=== git commit: $(git rev-parse --short HEAD)"

# Compile here (compute node). Plain build/, because tests/check.sh looks there.
make serial mpi || exit 1

status=0
echo "=== halo tests (make test-mpi)"
MPIRUN="srun --mpi=pmix -n" make test-mpi || status=1
echo "=== where the ranks ran (5 ranks):"
srun --mpi=pmix -n 5 hostname | sort | uniq -c
echo "=== make check (MPI vs serial, 1-4 ranks)"
MPIRUN="srun --mpi=pmix -n" make check || status=1

echo "=== done $(date -Is)  status=$status"
exit $status
