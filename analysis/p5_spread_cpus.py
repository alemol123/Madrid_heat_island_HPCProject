#!/usr/bin/env python3
"""Pick CPUs for MPI ranks spread evenly over the L3 caches (chiplets) of an allocation (P5).

Used by slurm/l3_spread_test.sh. Reads the node topology (`lscpu -e=CPU,CORE,SOCKET,NODE,CACHE`)
and the CPUs this job may use (Cpus_allowed_list from /proc/self/status), keeps one hardware
thread per physical core, groups the cores by L3 cache and takes them round-robin over the
caches, so R ranks land on as many chiplets as possible.

Prints a comma-separated CPU list for `srun --cpu-bind=map_cpu:<list>`, and a summary on stderr.

  python3 analysis/p5_spread_cpus.py --topology results/fortran_topology.txt --allowed "0-15,128-143" --ranks 16

Standard library only.
"""
import argparse
import sys


def parse_ranges(s):
    out = set()
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--topology", required=True)
    ap.add_argument("--allowed", required=True, help="CPU list, e.g. 0-15,128-143")
    ap.add_argument("--ranks", type=int, required=True)
    a = ap.parse_args()

    first_cpu_of_core, l3 = {}, {}
    for line in open(a.topology):
        p = line.split()
        if len(p) < 5 or not p[0].isdigit():
            continue
        cpu, core, cache = int(p[0]), int(p[1]), int(p[4].split(":")[-1])
        l3[cpu] = cache
        first_cpu_of_core[core] = min(cpu, first_cpu_of_core.get(core, cpu))

    allowed = parse_ranges(a.allowed)
    primaries = sorted(c for c in first_cpu_of_core.values() if c in allowed)
    groups = {}
    for c in primaries:
        groups.setdefault(l3[c], []).append(c)
    if sum(len(g) for g in groups.values()) < a.ranks:
        sys.exit(f"only {len(primaries)} physical cores allowed, need {a.ranks}")

    pick, order = [], sorted(groups)
    i = 0
    while len(pick) < a.ranks:
        g = groups[order[i % len(order)]]
        if g:
            pick.append(g.pop(0))
        i += 1
    per = {}
    for c in pick:
        per[l3[c]] = per.get(l3[c], 0) + 1
    print(f"spread: {a.ranks} ranks over {len(per)} L3 caches {dict(sorted(per.items()))} "
          f"(allocation has {len(primaries)} cores on {len(order)} L3 caches)", file=sys.stderr)
    print(",".join(map(str, pick)))


if __name__ == "__main__":
    main()
