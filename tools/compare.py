#!/usr/bin/env python3
"""Compare two output fields (docs/contract.md format) and report the difference.

    python3 tools/compare.py reference.bin candidate.bin [--rtol 1e-5] [--exact]

Passes (exit 0) when  max|a - b| <= rtol * max|a|   (relative to the field size;
1e-5 absolute is below float32 precision at ~300 K, see docs/contract.md).
--exact requires bitwise-identical fields (use for serial vs MPI).
Exit 1 = too different, exit 2 = files unreadable or different sizes.
Works with numpy if installed, otherwise with the standard library only.
"""
import argparse
import struct
import sys


def read_field(path):
    with open(path, "rb") as f:
        head = f.read(8)
        if len(head) != 8:
            sys.exit(f"{path}: missing header")
        nx, ny = struct.unpack("<ii", head)
        data = f.read()
    if len(data) != 4 * nx * ny:
        sys.exit(f"{path}: expected {nx}x{ny} floats, got {len(data) // 4} values")
    try:
        import numpy as np
        return nx, ny, np.frombuffer(data, dtype="<f4").astype("f8")
    except ImportError:
        import array
        a = array.array("f")
        a.frombytes(data)
        if sys.byteorder != "little":
            a.byteswap()
        return nx, ny, a


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("reference")
    p.add_argument("candidate")
    p.add_argument("--rtol", type=float, default=1e-5)
    p.add_argument("--exact", action="store_true")
    args = p.parse_args()

    nx, ny, a = read_field(args.reference)
    nx2, ny2, b = read_field(args.candidate)
    if (nx, ny) != (nx2, ny2):
        print(f"FAIL size mismatch: {nx}x{ny} vs {nx2}x{ny2}")
        sys.exit(2)

    try:
        import numpy as np
        diff = np.abs(a - b)
        k = int(np.argmax(diff))
        maxdiff, scale = float(diff[k]), float(np.max(np.abs(a)))
        nbad = int(np.count_nonzero(diff))
        nonfinite = not (np.all(np.isfinite(a)) and np.all(np.isfinite(b)))
    except ImportError:
        import math
        maxdiff, k, scale, nbad, nonfinite = 0.0, 0, 0.0, 0, False
        for idx, (x, y) in enumerate(zip(a, b)):
            if not (math.isfinite(x) and math.isfinite(y)):
                nonfinite = True
            d = abs(x - y)
            if d > 0:
                nbad += 1
            if d > maxdiff:
                maxdiff, k = d, idx
            scale = max(scale, abs(x))

    i, j = k % nx, k // nx
    tol = 0.0 if args.exact else args.rtol * scale
    ok = (maxdiff <= tol) and not nonfinite
    print(f"{'PASS' if ok else 'FAIL'}  {nx}x{ny}  max_abs_diff={maxdiff:.3e} at (i={i}, j={j})  "
          f"tolerance={tol:.3e}  cells_differing={nbad}"
          + ("  NON-FINITE VALUES" if nonfinite else ""))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
