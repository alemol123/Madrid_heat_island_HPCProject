| Grid | Ranks | Default: 2 chiplets (s) | Spread: 4 chiplets (s) | Spread faster | Repeats |
|---|---|---|---|---|---|
| 4096² | 8 | 0.232 | 0.120 | 1.93× | 5 + 5 |
| 4096² | 16 | 0.213 | 0.062 | 3.42× | 5 + 5 |
| 8192² | 8 | 0.315 | 0.177 | 1.78× | 5 + 5 |
| 8192² | 16 | 0.314 | 0.143 | 2.20× | 5 + 5 |

Compute time, medians. Same allocation (32 cores on 4 chiplets of fortran, AMD EPYC 9755, 32 MB L3 per 8-core chiplet), placements alternated in every repeat. Jobs 32611, 32612.
