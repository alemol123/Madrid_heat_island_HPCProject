| Grid | Nodes × ranks | Pairs | gather median (s) | MPI-IO median (s) | MPI-IO / gather (paired median) | 95% range | MPI-IO faster | Wilcoxon p | Holm-adjusted p |
|---|---|---|---|---|---|---|---|---|---|
| 4096² (67 MB) | 1 × 16 | 30 | 0.238 | 0.226 | 1.01 | 0.92–1.17 | 14/30 | 0.339 | 0.339 |
| 4096² (67 MB) | 2 × 16 | 30 | 0.256 | 0.213 | 0.89 | 0.81–0.96 | 21/30 | 0.047 | 0.094 |

Rows `iorep_gather` / `iorep_mpiio`, Slurm job(s) 31411. Pairs = alternating repeats; ratio < 1 means MPI-IO faster. Holm-adjusted p < 0.05 = significant after correcting for testing several configurations.
