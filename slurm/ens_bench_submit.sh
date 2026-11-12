#!/bin/bash
# Run every ensemble benchmark, ONE job at a time (the account limits are 8 jobs in the queue,
# 4 running, 64 CPUs in total; with --hint=nomultithread every task takes a whole core = 2 CPUs,
# so one job may have at most 32 tasks = 31 workers + 1 master).
#
# Run it on the login node with nohup so it keeps going after you log out:
#     nohup bash slurm/ens_bench_submit.sh 1 > results/logs/bench_driver.log 2>&1 &      # 1 repeat first
#     nohup bash slurm/ens_bench_submit.sh 3 > results/logs/bench_driver.log 2>&1 &      # then 3 repeats
# Watch:  tail -f results/logs/bench_driver.log      (Ctrl-C stops watching, not the runs)
# It is safe to start it again: runs that already finished are skipped.
#
# Needs: sbatch slurm/ens_build.sh done, and the job lists made with
#   python3 tools/make_sweep.py --city data/district_sol_retiro.city --field build/28548/field.bin --bench-steps 7100
#
# Runs (each repeated REPS times; W = workers = MPI ranks that run the solver, plus 1 master rank):
#   A  scheduling:  uniform, skewed, skewed_sorted lists  x  static, cyclic, dynamic    W=31, 1 node
#   B  scaling:     uniform and skewed lists, dynamic                                   W=2, 4, 8, 16 (W=31 is in A)
#   D  two nodes:   uniform and skewed lists, dynamic                                   W=31 on 2 nodes (16 tasks each)
# At the end it tries a Slurm job array of 20 tasks to record whether the account allows it.
set -uo pipefail
cd "$(dirname "$0")/.."
SBATCH=${SBATCH:-sbatch}
REPS=${1:-3}
D=data/sweep
O=results/ens_bench
mkdir -p results/logs "$O"

for l in uniform skewed skewed_sorted; do
  [ -e "$D/jobs_$l.txt" ] || { echo "missing $D/jobs_$l.txt: run tools/make_sweep.py first"; exit 1; }
done

# submit TAG LIST MODE WORKERS NODES  -> submits, waits for the job to end, skips finished runs
submit() {
  local tag=$1 list=$2 mode=$3 w=$4 nodes=$5
  if [ -s "$O/$tag/summary.csv" ]; then echo "skip $tag (done)"; return; fi
  local per=$(( (w + 1 + nodes - 1) / nodes ))          # tasks per node = (workers + 1 master) / nodes
  echo "$(date +%T) run $tag: $nodes node(s), $per tasks each"
  $SBATCH --wait --job-name="$tag" --nodes="$nodes" --ntasks-per-node="$per" \
      --export=ALL,JOBS=$D/jobs_$list.txt,MODE=$mode,TAG=$tag,OUT=$O/$tag,NOFIELDS=1 slurm/ensemble.sh \
      || echo "WARNING: $tag did not finish cleanly"
}

for r in $(seq 1 "$REPS"); do
  for list in uniform skewed skewed_sorted; do                       # A
    for mode in static cyclic dynamic; do
      submit "bench_${list}_${mode}_w31_n1_r$r" $list $mode 31 1
    done
  done
  for list in uniform skewed; do
    for w in 2 4 8 16; do                                            # B
      submit "bench_${list}_dynamic_w${w}_n1_r$r" $list dynamic $w 1
    done
    submit "bench_${list}_dynamic_w31_n2_r$r" $list dynamic 31 2     # D
  done
done

echo "$(date +%T) job-array probe (expected to be refused by the account limits):"
$SBATCH -p cpu --array=1-20 -t 1 --job-name=array_probe --wrap="sleep 5" 2>&1 | tee results/array_probe.txt
echo "$(date +%T) all done. See results/ensemble.csv"