| Grid | Nodes × ranks | Pairs | gather median (s) | MPI-IO median (s) | MPI-IO / gather (paired median) | 95% range | MPI-IO faster | Wilcoxon p | Holm-adjusted p |
|---|---|---|---|---|---|---|---|---|---|
| 4096² (67 MB) | 2 × 16 | 60 | 0.287 | 0.263 | 0.91 | 0.84–0.97 | 40/60 | 0.109 | 0.109 |

Rows `io2n_gather` / `io2n_mpiio`, Slurm job(s) 31413. Pairs = alternating repeats; ratio < 1 means MPI-IO faster. Holm-adjusted p < 0.05 = significant after correcting for testing several configurations.
