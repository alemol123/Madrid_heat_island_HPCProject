#!/bin/bash
# P1: halo-exchange tests for the MPI version (make test-mpi).
#   1. tests/test_halo.c on 1, 2, 3 and 5 ranks (ghost frame and one step vs serial)
#   2. heat_mpi with exactly one row per rank must equal serial bit for bit
#   3. heat_mpi must refuse more ranks than rows (exit code 2, no crash)
# Launcher: MPIRUN="srun --mpi=pmix -n" on the cluster (default: "mpirun -np").
set -u
cd "$(dirname "$0")/.."
B=${BUILD:-build}
OUT=$B/halo
mkdir -p "$OUT"
MPIRUN=${MPIRUN:-"mpirun -np"}
fail=0

for np in 1 2 3 5; do
  $MPIRUN $np $B/test_halo || fail=1
done

# One row per rank: 5 ranks, ny = 5
$B/heat_serial --nx 40 --ny 5 --steps 60 --seed 3 --out $OUT/serial_1row.bin > /dev/null || fail=1
if $MPIRUN 5 $B/heat_mpi --nx 40 --ny 5 --steps 60 --seed 3 --out $OUT/mpi_1row.bin > $OUT/mpi_1row.log 2>&1; then
  printf '%-34s ' "heat_mpi 5 ranks x 1 row"
  python3 tools/compare.py --exact $OUT/serial_1row.bin $OUT/mpi_1row.bin || fail=1
else
  echo "heat_mpi 5 ranks x 1 row: run failed (see $OUT/mpi_1row.log)"; fail=1
fi

# More ranks than rows must be refused cleanly (exit code 2)
$MPIRUN 5 $B/heat_mpi --nx 40 --ny 4 --steps 5 > $OUT/mpi_toofew.log 2>&1
rc=$?
if [ $rc -eq 2 ] && grep -q "too small" $OUT/mpi_toofew.log; then
  echo "heat_mpi refuses ny=4 on 5 ranks       PASS"
else
  echo "heat_mpi ny=4 on 5 ranks: expected exit code 2 and a message, got $rc (see $OUT/mpi_toofew.log)"; fail=1
fi

if [ $fail -eq 0 ]; then echo "halo tests: ALL PASSED"; else echo "halo tests: FAILED"; fi
exit $fail
