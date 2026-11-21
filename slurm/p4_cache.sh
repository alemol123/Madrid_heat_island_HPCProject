#!/bin/bash -l
#SBATCH --job-name=p4-cache
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=1G
#SBATCH --time=00:02:00
#SBATCH --output=results/logs/p4-cache-%j.out

echo "=== NODE ==="
hostname

echo "=== CACHE INFORMATION ==="
lscpu -C

echo "=== NUMA INFORMATION ==="
numactl --hardware
