#!/bin/bash -l
#SBATCH --job-name=p4-vector-report
#SBATCH --partition=cpu
#SBATCH --nodelist=julia
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:05:00
#SBATCH --output=results/logs/p4-vector-report-%j.out

set -euo pipefail

cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

echo "=== P4 GCC VECTORIZATION REPORT ==="
echo "Node: $(hostname)"
echo "Compiler: $(gcc --version | head -1)"

BUILD=build/p4_vector_report

mkdir -p "$BUILD"

make hybrid BUILD=$BUILD \
    CFLAGS="-O3 -march=native -std=c11 -Wall -Wextra -fopt-info-vec-all=$BUILD/vectorization.txt"

echo "=== VECTOR REPORT GENERATED ==="
echo "Location: $BUILD/vectorization.txt"

echo "=== STEP.C VECTOR REPORT ==="
grep -F "src/common/step.c" "$BUILD/vectorization.txt" || true

echo "=== END ==="
