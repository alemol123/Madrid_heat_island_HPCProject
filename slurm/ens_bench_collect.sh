#!/bin/bash
#   bash slurm/ens_bench_collect.sh
# Directory names look like  results/ens_bench/array_skewed_w32_r2  (workers = the number after w).
set -euo pipefail
cd "$(dirname "$0")/.."
n=0
for d in results/ens_bench/array_*; do
  [ -d "$d" ] || continue
  tag=$(basename "$d")
  w=$(echo "$tag" | sed -n 's/.*_w\([0-9]*\)_.*/\1/p')
  list=$(echo "$tag" | sed -n 's/^array_\(.*\)_w[0-9]*_r[0-9]*$/\1/p')
  expect=$(grep -cv '^[[:space:]]*\(#\|$\)' "data/sweep/jobs_$list.txt")
  grep -q "^$tag," results/ensemble.csv 2>/dev/null && { echo "skip $tag (already in ensemble.csv)"; continue; }
  python3 tools/array_wall.py "$d" --workers "$w" --tag "$tag" --expect "$expect" --csv results/ensemble.csv && n=$((n + 1))
done
echo "added $n array rows"
