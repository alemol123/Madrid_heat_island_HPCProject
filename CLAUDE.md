# Urban Heat Island Simulator (HPC course project)

2D heat diffusion on a city grid (stencil). Versions: serial, MPI, MPI+OpenMP, OpenACC.
The interface every version must follow is in docs/contract.md. Read it before writing code.

## Rules
- Never run long jobs on the login node; write sbatch scripts in slurm/ and submit them.
- Every version must match the serial output (make check / tools/compare.py).
- Keep the kernel in one function step(in,out,...). No large data files in git.
- Same CLI arguments and results CSV schema for all versions (see docs/contract.md).
- Add a test before adding a feature. Record compiler flags and module versions in env/README.md.
- Shared physical constants live in src/params.h only. Never hard-code them elsewhere.

## Build
source env/modules.sh; make serial mpi hybrid   # flags: -O3 -march=native
make check                                      # compare every version against serial

## Cluster
Slurm via sbatch/srun. MPI module and GPU partition names: see env/README.md.

## Git
One branch per person (p1-solver, p2-data, p3-mpi, p4-openmp, p5-gpu).
Small commits. Merge to main only by pull request, after make check passes and the reviewer approves.
Review chain: P1 reviews P2, P2 reviews P3, P3 reviews P4, P4 reviews P5, P5 reviews P1.
