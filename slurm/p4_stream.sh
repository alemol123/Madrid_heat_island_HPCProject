#!/bin/bash -l
#SBATCH --job-name=p4-stream
#SBATCH --partition=cpu
#SBATCH --nodelist=julia
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=4G
#SBATCH --time=00:05:00
#SBATCH --output=results/logs/p4-stream-%j.out

set -e

source env/modules.sh cpu

cd "$SLURM_SUBMIT_DIR/benchmarks/stream"

echo "=== STREAM BENCHMARK ==="
echo "Node: $(hostname)"
echo "Threads: $SLURM_CPUS_PER_TASK"

export OMP_NUM_THREADS=${THREADS:-$SLURM_CPUS_PER_TASK}
export OMP_PROC_BIND=close
export OMP_PLACES=cores

gcc -O3 -fopenmp -march=native \
    -DSTREAM_ARRAY_SIZE=50000000 \
    -DNTIMES=10 \
    stream.c -o stream

echo "=== RUNNING STREAM ==="
srun --cpu-bind=verbose,cores --mem-bind=verbose,local ./stream
