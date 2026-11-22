| Grid | Nodes × ranks | Pairs | gather median (s) | MPI-IO median (s) | MPI-IO / gather (paired median) | 95% range | MPI-IO faster | Wilcoxon p | Holm-adjusted p |
|---|---|---|---|---|---|---|---|---|---|
| 4096² (67 MB) | 1 × 16 | 30 | 0.288 | 0.266 | 0.94 | 0.90–0.98 | 21/30 | 0.147 | 0.588 |
| 4096² (67 MB) | 2 × 16 | 30 | 0.258 | 0.222 | 0.92 | 0.87–1.03 | 19/30 | 0.213 | 0.640 |
| 8192² (268 MB) | 1 × 16 | 30 | 0.878 | 0.768 | 1.04 | 0.74–1.18 | 14/30 | 0.644 | 1.000 |
| 8192² (268 MB) | 2 × 16 | 30 | 0.889 | 0.838 | 0.99 | 0.82–1.17 | 15/30 | 0.504 | 1.000 |

Rows `ioalt_gather` / `ioalt_mpiio`, Slurm job(s) 31409. Pairs = alternating repeats; ratio < 1 means MPI-IO faster. Holm-adjusted p < 0.05 = significant after correcting for testing several configurations.
