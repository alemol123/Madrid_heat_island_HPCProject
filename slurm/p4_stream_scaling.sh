#!/bin/bash -l
#SBATCH --job-name=p4-stream-scaling
#SBATCH --partition=cpu
#SBATCH --nodelist=julia
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/p4-stream-scaling-%j.out

set -euo pipefail

source env/modules.sh cpu

cd "$SLURM_SUBMIT_DIR/benchmarks/stream"

echo "=========================================="
echo "P4 STREAM THREAD SCALING EXPERIMENT"
echo "Node: $(hostname)"
echo "Array size: 50 million elements"
echo "Repetitions: 3"
echo "=========================================="

gcc -O3 -fopenmp -march=native \
    -DSTREAM_ARRAY_SIZE=50000000 \
    -DNTIMES=10 \
    stream.c -o stream

export OMP_PROC_BIND=close
export OMP_PLACES=cores
export OMP_DYNAMIC=FALSE

for threads in 1 2 4 8; do
    echo ""
    echo "##########################################"
    echo "THREAD CONFIGURATION: $threads"
    echo "##########################################"

    export OMP_NUM_THREADS=$threads

    for rep in 1 2 3; do
        echo ""
        echo "----- Threads: $threads | Repetition: $rep -----"

        srun --ntasks=1 \
             --cpus-per-task=$threads \
             --cpu-bind=verbose,cores \
             --mem-bind=verbose,local \
             ./stream
    done
done

echo ""
echo "=========================================="
echo "STREAM SCALING EXPERIMENT COMPLETED"
echo "=========================================="
