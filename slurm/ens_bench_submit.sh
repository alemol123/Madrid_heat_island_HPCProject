#!/bin/bash
# Submit every ensemble benchmark run. Not a batch script: run it on the login node with bash.
#   bash slurm/ens_bench_submit.sh        # 3 repeats of every run (about 60 small jobs)
#   bash slurm/ens_bench_submit.sh 1      # one repeat, to try it out
# Needs: sbatch slurm/ens_build.sh done, and the benchmark job lists made with
#   python3 tools/make_sweep.py --city data/district_sol_retiro.city --field build/28548/field.bin --bench-steps 7100
#
# Runs (each repeated REPS times; W = number of workers, a worker is one MPI rank running one solver):
#   A  scheduling:   uniform, skewed, skewed_sorted lists  x  static, cyclic, dynamic       at W=32, 1 node
#   B  scaling:      uniform and skewed lists, dynamic,    W = 4, 8, 16, 63, 1 node  (W=32 comes from A)
#   D  two nodes:    uniform and skewed lists, dynamic,    W = 63 on 2 nodes (compare with B at W=63)
#   C  job array:    uniform and skewed lists as a Slurm job array, at most 32 tasks at once
#                    (compare with A, dynamic, W=32)
# Results of A, B, D go to results/ensemble.csv by themselves. For C, afterwards run
#   bash slurm/ens_bench_collect.sh
set -euo pipefail
cd "$(dirname "$0")/.."
SBATCH=${SBATCH:-sbatch}
REPS=${1:-3}
D=data/sweep
O=results/ens_bench
mkdir -p results/logs "$O"

for l in uniform skewed skewed_sorted; do
  [ -e "$D/jobs_$l.txt" ] || { echo "missing $D/jobs_$l.txt: run tools/make_sweep.py first"; exit 1; }
done

# submit TAG LIST MODE WORKERS NODES
submit() {
  local tag=$1 list=$2 mode=$3 w=$4 nodes=$5
  local per=$(( (w + 1 + nodes - 1) / nodes ))          # tasks per node = (workers + 1 master) / nodes
  $SBATCH --job-name="$tag" --nodes="$nodes" --ntasks-per-node="$per" \
      --export=ALL,JOBS=$D/jobs_$list.txt,MODE=$mode,TAG=$tag,OUT=$O/$tag,NOFIELDS=1 slurm/ensemble.sh
}

count=0
for r in $(seq 1 "$REPS"); do
  for list in uniform skewed skewed_sorted; do                       # A
    for mode in static cyclic dynamic; do
      submit "bench_${list}_${mode}_w32_n1_r$r" $list $mode 32 1; count=$((count + 1))
    done
  done
  for list in uniform skewed; do
    for w in 4 8 16 63; do                                           # B
      submit "bench_${list}_dynamic_w${w}_n1_r$r" $list dynamic $w 1; count=$((count + 1))
    done
    submit "bench_${list}_dynamic_w63_n2_r$r" $list dynamic 63 2; count=$((count + 1))   # D
    n=$(grep -cv '^[[:space:]]*\(#\|$\)' "$D/jobs_$list.txt")        # C
    $SBATCH --job-name="array_${list}_w32_r$r" --array=1-"$n"%32 \
        --export=ALL,JOBS=$D/jobs_$list.txt,OUT=$O/array_${list}_w32_r$r,NOFIELDS=1 slurm/ensemble_array.sh
    count=$((count + 1))
  done
done
echo "submitted $count jobs. Watch them with: squeue -u \$USER"
echo "When the queue is empty: bash slurm/ens_bench_collect.sh   (adds the job-array rows)"
