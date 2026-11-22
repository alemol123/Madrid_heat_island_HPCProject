# Slide 5: Lessons learned and next steps (draft, P1)

Draft for the slide and the speaker notes. The EuroHPC part needs P2's plan and numbers (marked ⚠).

## On the slide

**Lessons learned**

1. **Agree on the interface first.** One contract (formats, CLI, CSV, tolerance) and one shared
   kernel let four versions plug in: MPI and hybrid match serial **bit for bit**.
2. **Measure before optimising.** The size sweep and compiler flags pointed at the
   coefficient lookup, not memory: an in-register lookup made the kernel **~4× faster**, same results.
3. **A faster kernel moves the bottleneck.** After the fix, the solver is limited by memory
   bandwidth (STREAM confirms the node's limit), so scaling flattens inside a node until the data
   fits in cache.
4. **Tests protect the team, not just the code.** `make check` and the halo tests caught
   real problems (a deleted check script, a wrong binary name) before they reached results.
5. **Make every number traceable.** Each result row records commit, Slurm job and node;
   shared nodes add noise, so we repeat runs and compare on the same node.

**Next step: EuroHPC** ⚠ (P2: numbers from the plan)

- Scale from one district to all of Madrid at 5 m (⚠ grid size) and many scenarios × weather days
- Target: ⚠ system (e.g. GPU partition of a EuroHPC machine), estimated ⚠ node-hours from our measured time per cell update

## Speaker notes (about 1 minute)

- We wrote the contract in week 1, before any code. Because every version calls the same
  `step()` on padded arrays, the MPI version could be checked bit for bit against serial, and
  any difference meant a bug, not rounding.
- The serial study showed time per cell barely changed between cache and main memory, and
  wider vectors didn't help; that ruled out memory and arithmetic and pointed at the per-cell
  table lookup. Keeping the six coefficients in vector registers gave 3.9–4.5×, bitwise identical.
- After that, one core moves about 50 GB/s, so a few cores saturate a node's memory: this is why
  MPI speed-up flattens between 4 and 16 ranks and jumps once each rank's block fits in L3 (P3's results).
- Verification: a relative tolerance (FMA alone changes results by 4·10⁻⁵), exact comparison for MPI,
  and dedicated halo tests over two nodes.
- ⚠ EuroHPC: P2 presents the plan; these numbers come from it.
