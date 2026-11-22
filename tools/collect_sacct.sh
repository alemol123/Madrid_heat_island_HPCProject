#!/bin/bash
# Collect Slurm accounting records (sacct) for every job behind the results in the repository.
# P1, before the freeze. Run on the login node from the repo root:
#     bash tools/collect_sacct.sh
# Job IDs are taken from the slurm_jobid column of results/*.csv and from the job ID in the
# names of results/logs/*_<jobid>.out. Output: results/sacct_all.txt (one line per job, '|'-separated).
# If Slurm hides other users' jobs (PrivateData), the missing IDs are listed at the end: their
# owners run the same script and commit the file again.
set -u
cd "$(dirname "$0")/.."
OUT=results/sacct_all.txt

ids=$(
  {
    for f in results/*.csv; do
      python3 - "$f" <<'EOF'
import csv, sys
try:
    with open(sys.argv[1], newline="") as fh:
        for row in csv.DictReader(fh):
            j = (row.get("slurm_jobid") or "").strip()
            if j.isdigit():
                print(j)
except Exception:
    pass
EOF
    done
    ls results/logs/ 2>/dev/null | sed -nE 's/.*_([0-9]{4,})\.out$/\1/p'
  } | sort -un
)
n=$(echo "$ids" | grep -c .)
echo "found $n job IDs in results/"
[ "$n" -eq 0 ] && exit 1

fmt=JobID,JobName%30,User,Partition,NodeList%30,NNodes,NTasks,AllocCPUS,Elapsed,TotalCPU,MaxRSS,State,Start,End
sacct -X -P -j "$(echo $ids | tr ' ' ',')" --format=$fmt > "$OUT.tmp"
# also the job steps' memory (MaxRSS is only filled in for steps)
sacct -P -j "$(echo $ids | tr ' ' ',')" --format=JobID,MaxRSS,AveCPU,Elapsed,State > "${OUT%.txt}_steps.txt"
mv "$OUT.tmp" "$OUT"

found=$(tail -n +2 "$OUT" | cut -d'|' -f1 | sort -u)
missing=$(comm -23 <(echo "$ids") <(echo "$found"))
echo "wrote $OUT ($(echo "$found" | grep -c .) jobs) and ${OUT%.txt}_steps.txt"
if [ -n "$missing" ]; then
  echo "not visible to $USER (other users' jobs, or too old): $(echo $missing)"
fi
