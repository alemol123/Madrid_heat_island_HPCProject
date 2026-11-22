#!/bin/bash
# Baseline for the MPI ensemble: the SAME job list as a Slurm job array, one array task per job.
# Run  sbatch slurm/ens_build.sh  first. Then from the repo root, N = number of jobs in the list and
# K = how many tasks may run at once (use K = the number of workers of the MPI run you compare with):
#   sbatch --array=1-256%64 --export=ALL,JOBS=data/sweep/jobs_skewed.txt,OUT=results/ens_bench/array_skew_64,NOFIELDS=1 slurm/ensemble_array.sh
# When all tasks are done:
#   python3 tools/array_wall.py results/ens_bench/array_skew_64 --workers 64 --tag array_skew_64 --csv results/ensemble.csv
#SBATCH --job-name=ens_array
#SBATCH --partition=cpu
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=1G
#SBATCH --time=00:30:00
#SBATCH --output=results/logs/%x_%A_%a.out

set -uo pipefail
cd "$SLURM_SUBMIT_DIR"
JOBS=${JOBS:-data/sweep/jobs_science.txt}
OUT=${OUT:-results/ens_array_$SLURM_ARRAY_JOB_ID}
NOFIELDS=${NOFIELDS:-0}
mkdir -p "$OUT/fields" "$OUT/timing" "$OUT/logs"

# line number ID of the job list, ignoring comments and blank lines
line=$(grep -v '^[[:space:]]*\(#\|$\)' "$JOBS" | sed -n "${SLURM_ARRAY_TASK_ID}p")
[ -n "$line" ] || { echo "no job $SLURM_ARRAY_TASK_ID in $JOBS"; exit 1; }
read -r name city steps extra <<< "$line"

out="$OUT/fields/$name.bin"
[ "$NOFIELDS" = "1" ] && out=/dev/null

start=$(date +%s.%N)
# the solver was built by slurm/ens_build.sh (no compiling here)
build/ens/heat_serial --city "$city" --steps "$steps" $extra --out "$out" \
    --timing "$OUT/timing/$name.csv" --tag "$name" > "$OUT/logs/$name.log" 2>&1
status=$?
end=$(date +%s.%N)
echo "$name $start $end $status" >> "$OUT/array_times.txt"
exit $status
