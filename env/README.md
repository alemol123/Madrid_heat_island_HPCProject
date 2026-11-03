# Environment and cluster notes

Cluster survey and environment tests by Aakriti (P5), 3 November 2026
(sinfo, scontrol, module avail, lscpu, nvidia-smi, srun --mpi=list).

## Quick start

    source env/modules.sh cpu    # GCC + OpenMPI + Python (serial, MPI, OpenMP)
    source env/modules.sh gpu    # NVIDIA HPC SDK (OpenACC)

Never load both in the same session (see "GPU environment" below).

## Nodes

| Node    | CPU                                         | Memory   | GPUs                                         |
|---------|---------------------------------------------|----------|----------------------------------------------|
| haskell | 2 sockets x 128 cores = 256 cores / 512 thr | ~397 GB  | 2 x RTX 6000 Ada, 48 GB each (cc89)          |
| julia   | 512 threads                                 | ~503 GB  | 1 x RTX PRO 6000 Blackwell, 96 GB (cc120)    |
| fortran | 2 x AMD EPYC 9755, 256 cores / 512 threads  | ~738 GB  | 2 x RTX PRO 6000 Blackwell (cc120), memory: TO CONFIRM |

- Login node: `rust`. It has an older CPU (zen2). Do not compile or run experiments there.
- fortran's CPU (AMD EPYC 9755) supports AVX-512; haskell and julia CPU models not checked yet.

## Partitions

| Partition       | Max time | Default time |
|-----------------|----------|--------------|
| cpu (default)   | 7 days   | 4 h          |
| gpu             | 3 days   | 4 h          |
| interactive     | 8 h      | -            |

All three partitions use the same three nodes.

Important for measurements:
- `OverSubscribe=FORCE:2`: up to two jobs can share the same resources.
  `--exclusive` cannot be guaranteed, so timings can be disturbed by other jobs.
  Always repeat runs (at least 3) and report mean and spread.
- `PreemptMode=REQUEUE`: higher-priority jobs can stop and requeue yours.
  Keep jobs short.
- CPU limits are enforced: always set `--cpus-per-task` and match
  `OMP_NUM_THREADS` to it.
- Always set `--time` short (the default is 4 h; short jobs start sooner).

## CPU environment (`source env/modules.sh cpu`)

- GCC 13.2.0
- OpenMPI 4.1.6 (with PMIx 4.2.6, UCX 1.15.0)
- Python 3.11.5
- Tested on a compute node, 3 Nov 2026: modules load, `gcc` and `mpicc` found.

Launching MPI: Slurm supports `pmix` (also `pmi2`), so use

    srun --mpi=pmix ./program

Compiling: the shared software stack is built for an older CPU (zen2).
Compile our own code on a compute node (inside `srun` or `sbatch`) with
`-O3 -march=native` so it uses the compute nodes' CPU features.

## GPU environment (`source env/modules.sh gpu`)

- NVIDIA HPC SDK 25.7: `nvc` / `nvc++` / `nvfortran` (OpenACC), CUDA 12.9 (`nvcc`), its own MPI, NCCL.
- Do not combine with the CPU environment: nvhpc changes which `mpicc` is found
  and puts `/usr/bin` first in PATH.
- OpenACC compile (on a GPU node):

      nvc -acc -gpu=ccnative -Minfo=accel prog.c -o prog

  `ccnative` targets the GPU of the node you compile on. To compile for a
  specific GPU type: `-gpu=cc89` (Ada, haskell) or `-gpu=cc120` (Blackwell, julia/fortran).
- Course CUDA container (alternative): `module load hpc-course-cuda/12.8.1`, then
  `cuda-exec nvcc -arch=sm_89 ...` (Ada) or `-arch=sm_120` (Blackwell).

## GPU experiment rules

- Request one fixed GPU type for a set of measurements, not a specific node:

      --gres=gpu:rtx6000ada:1     # Ada (haskell)
      --gres=gpu:rtx6000bb:1      # Blackwell (julia or fortran)

- Print `nvidia-smi` in every GPU job log, so each result records which GPU it used.
- GPUs are busy: on 3 Nov all 5 GPUs were in use (also by jobs from the cpu
  partition), and a 5-minute test job was estimated to start about 13 hours later.
  Submit GPU jobs early with sbatch and keep them short.

## Profiling tools

- LIKWID: not available (no module).
- perf: not found on the login node (compute nodes not checked).
- Use instead: our own timers (MPI_Wtime / omp_get_wtime), STREAM for memory
  bandwidth, compiler reports (`-fopt-info-vec`, `-Minfo=accel`), `nvidia-smi`.
- Nsight Systems (`nsys`): TO CONFIRM.

## Working tips

- Use `sbatch` for anything that may wait in the queue; `srun` blocks your terminal.
- `export LMOD_PAGER=none` makes `module show` / `module avail` print without a pager
  (already set by modules.sh).
- Check your jobs: `squeue -u <username>`; job accounting: `sacct -j <jobid>`.