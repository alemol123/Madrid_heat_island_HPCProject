#!/bin/bash -l
#SBATCH --job-name=p4-inspect
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=2G
#SBATCH --time=00:02:00
#SBATCH --output=results/logs/p4-inspect-%j.out

set -e

echo "=== COMPUTE NODE ==="
hostname

echo "=== CPU INFORMATION ==="
lscpu | grep -E 'Model name|CPU\(s\)|Socket|Core|Thread'

echo "=== SLURM ALLOCATION ==="
echo "CPUs: $SLURM_CPUS_PER_TASK"
echo "Node: $SLURMD_NODENAME"
