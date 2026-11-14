#!/bin/bash
# make check: every version must reproduce the serial output.
#   1. serial vs the saved reference (tests/ref/), catches accidental changes
#   2. every other built version vs serial on the same case
# Run it on a compute node for the parallel versions, e.g.
#   srun -p cpu -n 4 --cpus-per-task=2 --time=00:10:00 make check
# MPI launcher: MPIRUN="srun --mpi=pmix -n" on the cluster, "mpirun -np" locally.
set -u
cd "$(dirname "$0")/.."
B=build
OUT=$B/check
mkdir -p "$OUT"
MPIRUN=${MPIRUN:-"mpirun -np"}
fail=0

run_cmp() {   # name, compare-options, reference, candidate
  printf '%-34s ' "$1"
  if python3 tools/compare.py $2 "$3" "$4"; then :; else fail=1; fi
}

# ---- 1. serial against the saved reference ----
REF=tests/ref/ref_seed42_256x256_200.bin
$B/heat_serial --nx 256 --ny 256 --steps 200 --city synthetic --seed 42 \
    --out $OUT/serial_256.bin > $OUT/serial_256.log || { echo "serial run failed"; exit 1; }
run_cmp "serial vs tests/ref" "" "$REF" "$OUT/serial_256.bin"

# Second case with a size that does not divide evenly by 2, 3, 4 ranks
$B/heat_serial --nx 250 --ny 251 --steps 120 --city synthetic --seed 7 \
    --out $OUT/serial_odd.bin > $OUT/serial_odd.log || { echo "serial run failed"; exit 1; }

# ---- 2. other versions vs serial (P3, P4, P5 add theirs here) ----
# MPI (P3): must be bitwise identical to serial, so --exact.
if [ -x $B/heat_mpi ]; then
  for np in 1 2 3 4; do
    for case in "256 256 200 42 serial_256" "250 251 120 7 serial_odd"; do
      set -- $case
      $MPIRUN $np $B/heat_mpi --nx $1 --ny $2 --steps $3 --city synthetic --seed $4 \
          --out $OUT/mpi_${np}_$5.bin > $OUT/mpi_${np}_$5.log 2>&1 \
        || { echo "mpi np=$np run failed (see $OUT/mpi_${np}_$5.log)"; fail=1; continue; }
      run_cmp "mpi np=$np $5" "--exact" "$OUT/$5.bin" "$OUT/mpi_${np}_$5.bin"
    done
  done
else
  echo "mpi: not built, skipped"
fi

# Hybrid MPI+OpenMP (P4): same results; relative tolerance allows SIMD/FMA reordering.
if [ -x $B/heat_hybrid ]; then
  for cfg in "1 4" "2 2" "3 1"; do
    set -- $cfg
    OMP_NUM_THREADS=$2 $MPIRUN $1 $B/heat_hybrid --nx 250 --ny 251 --steps 120 --city synthetic --seed 7 \
        --out $OUT/hybrid_$1x$2.bin > $OUT/hybrid_$1x$2.log 2>&1 \
      || { echo "hybrid $1x$2 run failed"; fail=1; continue; }
    run_cmp "hybrid ${1}x${2}" "" "$OUT/serial_odd.bin" "$OUT/hybrid_$1x$2.bin"
  done
else
  echo "hybrid: not built, skipped"
fi

# OpenACC (P5): relative tolerance (different operation order on the GPU).
if [ -x $B/heat_acc ]; then
  $B/heat_acc --nx 250 --ny 251 --steps 120 --city synthetic --seed 7 \
      --out $OUT/acc_odd.bin > $OUT/acc_odd.log 2>&1 || { echo "openacc run failed"; fail=1; }
  [ -f $OUT/acc_odd.bin ] && run_cmp "openacc" "" "$OUT/serial_odd.bin" "$OUT/acc_odd.bin"
else
  echo "openacc: not built, skipped"
fi

if [ $fail -eq 0 ]; then echo "make check: ALL PASSED"; else echo "make check: FAILED"; fi
exit $fail
