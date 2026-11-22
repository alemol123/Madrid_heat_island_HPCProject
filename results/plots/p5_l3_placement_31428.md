| Ranks | L3 caches used | Total L3 (MB) | Working set per L3 (MB) | Fits? | Compute (s, median) | Speed-up vs 1 rank |
|---|---|---|---|---|---|---|
| 1 | 1 | 32 | 151.1 | no | 1.064 | 1.0× |
| 2 | 2 | 64 | 75.6 | no | 0.531 | 2.0× |
| 4 | 2 | 64 | 75.6 | no | 0.282 | 3.8× |
| 8 | 2 | 64 | 75.6 | no | 0.227 | 4.7× |
| 16 | 2 | 64 | 75.6 | no | 0.208 | 5.1× |
| 32 | 4 | 128 | 37.8 | no | 0.038 | 28.3× |

4096×4096: working set 151 MB (two float grids + city). L3 = 32 MB per cache. 'L3 caches used' lists every count seen over the repeats (placement can differ between repeats). Log results/logs/scaling_mpi_31428.out, job 31428.
