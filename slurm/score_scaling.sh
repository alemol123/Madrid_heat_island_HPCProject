#!/bin/bash
# OpenMP scaling of tools/score_candidates.c: threads 1..32 and the three loop schedules.
# One job, one node, 32 cores (with --hint=nomultithread a core counts as 2 CPUs: 64 = the account limit).
#
# First make a temperature field for a big city (once), e.g. the 4096 x 4096 one:
#   sbatch --export=ALL,CITY=data/city_4096.city,STEPS=1420,TAG=c4096,REPS=1 slurm/serial.sh
#   -> the field is build/<jobid>/field.bin
# Then, from the repo root:
#   sbatch --export=ALL,CITY=data/city_4096.city,FIELD=build/<jobid>/field.bin slurm/score_scaling.sh
# Results are appended to results/score_scaling.csv
#SBATCH --job-name=score_scaling
#SBATCH --partition=cpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --hint=nomultithread
#SBATCH --mem=12G
#SBATCH --time=00:40:00
#SBATCH --output=results/logs/%x_%j.out

set -euo pipefail
cd "$(dirname "${SLURM_SUBMIT_DIR:-$PWD}/x")"
source env/modules.sh cpu

CITY=${CITY:-data/city_4096.city}
FIELD=${FIELD:?set FIELD=build/<jobid>/field.bin (see the header of this script)}
RADIUS=${RADIUS:-40}
TOP=${TOP:-5000}
REPS=${REPS:-3}
THREADS=${THREADS:-"1 2 4 8 16 32"}
CSV=${CSV:-results/score_scaling.csv}
[ -e "$CITY" ] && [ -e "$FIELD" ] || { echo "missing $CITY or $FIELD"; exit 1; }

BUILD=build/$SLURM_JOB_ID
mkdir -p "$BUILD"
gcc -O3 -march=native -fopenmp -std=c11 -Wall -Wextra -o "$BUILD/score_candidates" tools/score_candidates.c -lm

echo "=== job $SLURM_JOB_ID on $(hostname) at $(date -Is)"
echo "=== git commit: $(git rev-parse --short HEAD)"
echo "=== city=$CITY field=$FIELD radius=$RADIUS top=$TOP reps=$REPS threads=$THREADS"
export OMP_PLACES=cores OMP_PROC_BIND=close

run() {  # run SCHEDULE THREADS REP
  OMP_NUM_THREADS=$2 "$BUILD/score_candidates" --city "$CITY" --field "$FIELD" --radius "$RADIUS" --top "$TOP" \
      --threads "$2" --schedule "$1" --out "$BUILD/out.city" --timing-csv "$CSV" --tag "score_${1//,/-}_t$2_r$3"
}

# A: scaling with the default schedule (dynamic, chunk 4)
for r in $(seq 1 "$REPS"); do
  for t in $THREADS; do run dynamic,4 "$t" "$r"; done
done
# B: the three schedules at the largest thread count
maxt=$(echo $THREADS | tr ' ' '\n' | sort -n | tail -1)
for r in $(seq 1 "$REPS"); do
  for sch in static dynamic,1 dynamic,64 guided; do run "$sch" "$maxt" "$r"; done
done
echo "=== done $(date -Is)"