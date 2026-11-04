#!/bin/bash
# Usage: source env/modules.sh cpu   (GCC + OpenMPI, for serial/MPI/OpenMP)
#        source env/modules.sh gpu   (NVIDIA HPC SDK, for OpenACC)
export LMOD_PAGER=none
module purge
case "${1:-cpu}" in
  cpu) module load GCC/13.2.0 OpenMPI/4.1.6-GCC-13.2.0 Python/3.11.5-GCCcore-13.2.0 ;;
  gpu) module load nvhpc/25.7
       # CUPTI library for GPU profiling (avoids "libcupti.so not found")
       export LD_LIBRARY_PATH="$NVHPC_ROOT/cuda/12.9/extras/CUPTI/lib64:${LD_LIBRARY_PATH}" ;;
  *)   echo "Usage: source env/modules.sh [cpu|gpu]"; return 1 ;;
esac
module list
which gcc mpicc nvc 2>/dev/null || true