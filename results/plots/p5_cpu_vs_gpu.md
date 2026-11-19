| Grid (steps) | Variant | Repeats | Loop (s) | ns per cell update | End-to-end (s) | Loop speed-up vs CPU 32 cores | Node |
|---|---|---|---|---|---|---|---|
| 2048² (953) | CPU, 1 core (AVX-512) | 5 | 0.8599 | 0.2151 | 0.872 | 0.05× | fortran |
| 2048² (953) | CPU, 32 cores (MPI) | 5 | 0.0430 | 0.0108 | 0.043 | 1.00× | fortran |
| 2048² (953) | GPU, resident | 3 | 0.0422 | 0.0106 | 0.185 | 1.02× | haskell |
| 2048² (953) | GPU, resident + --bc-once | 3 | 0.0220 | 0.0055 | 0.164 | 1.95× | haskell |
| 4096² (238) | CPU, 1 core (AVX-512) | 5 | 1.0980 | 0.2750 | 1.148 | 0.04× | fortran |
| 4096² (238) | CPU, 32 cores (MPI) | 5 | 0.0470 | 0.0118 | 0.049 | 1.00× | fortran |
| 4096² (238) | GPU, resident | 3 | 0.0505 | 0.0127 | 0.270 | 0.93× | haskell |
| 4096² (238) | GPU, resident + --bc-once | 3 | 0.0454 | 0.0114 | 0.260 | 1.04× | haskell |
| 8192² (59) | CPU, 1 core (AVX-512) | 5 | 1.2422 | 0.3137 | 1.442 | 0.13× | fortran |
| 8192² (59) | CPU, 32 cores (MPI) | 5 | 0.1600 | 0.0404 | 0.167 | 1.00× | fortran |
| 8192² (59) | GPU, resident | 3 | 0.0459 | 0.0116 | 0.533 | 3.49× | haskell |
| 8192² (59) | GPU, resident + --bc-once | 3 | 0.0446 | 0.0113 | 0.535 | 3.59× | haskell |

Same work in every row of a grid size. Loop: GPU = time_compute (copies excluded); CPU = time_total - time_io. End-to-end = time_total (GPU includes start-up and copies). Medians of the repeats; 32 cores is the account limit (64 CPUs counted as 2 threads per core).
