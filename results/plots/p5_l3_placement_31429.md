| Ranks | L3 caches used | Total L3 (MB) | Working set per L3 (MB) | Fits? | Compute (s, median) | Speed-up vs 1 rank |
|---|---|---|---|---|---|---|
| 1 | 1 | 32 | 37.8 | no | 0.851 | 1.0× |
| 2 | 2 | 64 | 18.9 | yes | 0.337 | 2.5× |
| 4 | 2 | 64 | 18.9 | yes | 0.168 | 5.1× |
| 8 | 2 | 64 | 18.9 | yes | 0.091 | 9.3× |
| 16 | 2 | 64 | 18.9 | yes | 0.049 | 17.3× |
| 32 | 4 | 128 | 9.5 | yes | 0.026 | 32.5× |

2048×2048: working set 38 MB (two float grids + city). L3 = 32 MB per cache. 'L3 caches used' lists every count seen over the repeats (placement can differ between repeats). Log results/logs/scaling_mpi_31429.out, job 31429.
