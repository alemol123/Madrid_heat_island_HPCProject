#!/bin/bash
# Build the ensemble tools once, on a compute node (-march=native must see the compute CPU).
# Submit from the repo root:   sbatch slurm/ens_build.sh
# Creates build/ens/heat_serial and build/ens/run_ensemble. Run it again after changing the code.
#SBATCH --job-name=ens_build
#SBATCH --partition=cpu
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
source env/modules.sh cpu

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)"
make serial BUILD=build/ens
mpicc -O2 -Wall -Wextra -o build/ens/run_ensemble tools/run_ensemble.c
ls -la build/ens
echo "=== done"
