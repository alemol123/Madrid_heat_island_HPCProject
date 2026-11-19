| Ranks | L3 caches used | Total L3 (MB) | Working set per L3 (MB) | Fits? | Compute (s, median) | Speed-up vs 1 rank |
|---|---|---|---|---|---|---|
| 1 | 1 | 32 | 604.2 | no | 1.111 | 1.0× |
| 2 | 2 | 64 | 302.1 | no | 0.535 | 2.1× |
| 4 | 2 | 64 | 302.1 | no | 0.350 | 3.2× |
| 8 | 2 | 64 | 302.1 | no | 0.315 | 3.5× |
| 16 | 2 | 64 | 302.1 | no | 0.313 | 3.5× |
| 32 | 4 | 128 | 151.1 | no | 0.139 | 8.0× |

8192×8192: working set 604 MB (two float grids + city). L3 = 32 MB per cache. 'L3 caches used' lists every count seen over the repeats (placement can differ between repeats). Log results/logs/scaling_mpi_32605.out, job 32605.
