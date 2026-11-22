| Grid | Condition | naive (µs) | resident (µs) | naive faster by | Jobs |
|---|---|---|---|---|---|
| 4096² | normal | 164.5 | 184.7 | 10.9% | 28698, 28697 |
| 4096² | L2 flushed every step | 208.4 | 208.6 | 0.1% | 31361, 31362 |
| 8192² | normal | 734.9 | 748.1 | 1.8% | 31363, 31364 |

Average GPU time of the step_rows kernel per step (Nsight Systems, cuda_gpu_kern_sum, 200 steps). --flush-l2 256 writes 256 MB before every step, evicting the grid from the 96 MB L2 cache of the RTX 6000 Ada.
