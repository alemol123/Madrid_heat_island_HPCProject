#!/bin/bash
# Stand-in for heat_serial, used only by tests/test_ensemble.sh.
# Takes the same options as the real solver, "computes" for steps*0.1 ms, writes the output
# and timing files. A job whose --tag starts with "fail" exits with status 3.
steps=0; out=/dev/null; timing=""; tag="x"; city=""
while [ $# -gt 0 ]; do
  case "$1" in
    --steps) steps=$2; shift 2;;
    --out) out=$2; shift 2;;
    --timing) timing=$2; shift 2;;
    --tag) tag=$2; shift 2;;
    --city) city=$2; shift 2;;
    *) shift;;
  esac
done
sleep "$(awk -v s="$steps" 'BEGIN{printf "%.4f", s*0.0001}')"
echo "fake field for $tag from $city" > "$out"
[ -n "$timing" ] && echo "tag,steps
$tag,$steps" > "$timing"
case "$tag" in fail*) echo "simulated failure" >&2; exit 3;; esac
exit 0
