#!/bin/bash
# Test of tools/run_ensemble.c with a fake solver (no real simulation, takes about 15 s).
#
# Run it where MPI works, e.g. inside an interactive allocation or a short batch job:
#     source env/modules.sh cpu
#     srun --mpi=pmix -n 1 true      # (check MPI works)
#     MPIRUN="srun --mpi=pmix" bash tests/test_ensemble.sh      # needs >= 5 tasks: see NP below
# or with mpirun on a laptop:   MPIRUN=mpirun bash tests/test_ensemble.sh
#
# MPIRUN is called as:  $MPIRUN -np N program ...   (srun: set MPIRUN_NP_FLAG="-n")
set -u
cd "$(dirname "$0")/.."
MPIRUN=${MPIRUN:-mpirun}
NPFLAG=${MPIRUN_NP_FLAG:--np}
EXE=${ENSEMBLE_EXE:-build/run_ensemble}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAKE=$PWD/tests/fake_solver.sh
chmod +x "$FAKE"

if [ -z "${ENSEMBLE_EXE:-}" ]; then
  mkdir -p build && mpicc -O2 -Wall -Wextra -o "$EXE" tools/run_ensemble.c || { echo "compile failed"; exit 1; }
fi

# 24 jobs: the 4 heavy ones (0.3 s each) come FIRST, then 20 light ones (0.03 s each).
{ for i in 0 1 2 3; do echo "heavy$i city.city 3000"; done
  for i in $(seq 0 19); do echo "light$i city.city 300"; done; } > "$WORK/skewed.txt"

fail=0
check() { if [ "$2" = "ok" ]; then echo "ok   $1"; else echo "FAIL $1 ($2)"; fail=1; fi; }
run() {  # run MODE NP JOBS OUTDIR [extra args]
  local mode=$1 np=$2 jobs=$3 out=$4; shift 4
  $MPIRUN $NPFLAG "$np" "$EXE" --jobs "$jobs" --solver "$FAKE" --mode "$mode" --outdir "$out" \
      --result-csv "$WORK/result.csv" --tag "t_$mode" "$@" > "$out.log" 2>&1
}
col() {  # col CSV NAME  -> value of header NAME for every row, one per line
  python3 - "$1" "$2" <<'EOF'
import csv, sys
for r in csv.DictReader(open(sys.argv[1])):
    print(r[sys.argv[2]])
EOF
}
wall() { grep -o 'wall=[0-9.]*' "$1.log" | head -1 | cut -d= -f2; }

# ---- 1. dynamic: every job exactly once, every output file there
run dynamic 5 "$WORK/skewed.txt" "$WORK/dyn"; rc=$?
check "dynamic exit code 0" "$([ $rc -eq 0 ] && echo ok || echo "rc=$rc")"
n=$(col "$WORK/dyn/summary.csv" name | sort -u | wc -l)
check "dynamic: 24 distinct jobs in summary" "$([ "$n" -eq 24 ] && echo ok || echo "got $n")"
check "dynamic: 24 timing files" "$([ "$(ls "$WORK/dyn/timing" | wc -l)" -eq 24 ] && echo ok || echo no)"
check "dynamic: 24 field files" "$([ "$(ls "$WORK/dyn/fields" | wc -l)" -eq 24 ] && echo ok || echo no)"
check "dynamic: workers are ranks 1-4 only" "$([ "$(col "$WORK/dyn/summary.csv" worker | sort -u | tr '\n' ' ')" = "1 2 3 4 " ] && echo ok || echo no)"

# ---- 2. static block vs dynamic on the same skewed list: dynamic must be clearly faster
run static 5 "$WORK/skewed.txt" "$WORK/sta"
ws=$(wall "$WORK/sta"); wd=$(wall "$WORK/dyn")
check "dynamic faster than static block on a skewed list ($wd s vs $ws s)" \
  "$(python3 -c "print('ok' if $wd < 0.7*$ws else 'no')")"
# block structure: worker numbers never decrease along the job list
mono=$(col "$WORK/sta/summary.csv" worker | python3 -c "
import sys; w=[int(x) for x in sys.stdin]; print('ok' if w==sorted(w) else 'no')")
check "static: contiguous blocks" "$mono"

# ---- 3. cyclic: job j on worker (j mod 4)+1
run cyclic 5 "$WORK/skewed.txt" "$WORK/cyc"
cyc=$(col "$WORK/cyc/summary.csv" worker | python3 -c "
import sys; w=[int(x) for x in sys.stdin]; print('ok' if all(x==(j%4)+1 for j,x in enumerate(w)) else 'no')")
check "cyclic: job j on worker j mod 4" "$cyc"

# ---- 4. a failing job is reported, the others still run, exit code is non-zero
{ echo "fail_one city.city 100"; for i in 1 2 3 4 5 6; do echo "ok$i city.city 100"; done; } > "$WORK/fail.txt"
run dynamic 4 "$WORK/fail.txt" "$WORK/fail"; rc=$?
check "failure: exit code non-zero" "$([ $rc -ne 0 ] && echo ok || echo "rc=$rc")"
st=$(col "$WORK/fail/summary.csv" status | sort | uniq -c | tr -s ' ' | tr '\n' ';')
check "failure: one job with status 3, six with 0" "$([ "$st" = " 6 0; 1 3;" ] && echo ok || echo "$st")"

# ---- 5. more workers than jobs
run dynamic 8 "$WORK/fail.txt" "$WORK/many" ; col "$WORK/many/summary.csv" name | sort -u | wc -l | grep -q '^7$'
check "more workers than jobs: all 7 jobs ran once" "$([ $? -eq 0 ] && echo ok || echo no)"

# ---- 6. one rank only: runs everything itself
run dynamic 1 "$WORK/fail.txt" "$WORK/one"
check "single rank runs all 7 jobs" "$([ "$(col "$WORK/one/summary.csv" name | wc -l)" -eq 7 ] && echo ok || echo no)"

# ---- 7. result CSV has a header and one row per run
rows=$(wc -l < "$WORK/result.csv")
check "result csv: header + 6 rows" "$([ "$rows" -eq 7 ] && echo ok || echo "lines=$rows")"

# ---- 8. bad inputs are refused with a message
$MPIRUN $NPFLAG 2 "$EXE" --jobs "$WORK/nothing.txt" --solver "$FAKE" --outdir "$WORK/x" > "$WORK/bad.log" 2>&1
check "missing job file refused" "$([ $? -ne 0 ] && grep -q 'job file' "$WORK/bad.log" && echo ok || echo no)"

[ $fail -eq 0 ] && echo "ALL ENSEMBLE TESTS PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
