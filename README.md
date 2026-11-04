# Urban Heat Island Simulator (Madrid)

We simulate how heat builds up in a Madrid district and use HPC to find
where trees and green roofs would cool it most.

## Team
- Person 1: Serial solver, tests, baseline, repo and REPRODUCE.md
- Person 2: City data, scenarios, ensemble runner, EuroHPC plan
- Person 3: MPI, strong scaling, overlap optimization, report editor
- Person 4: OpenMP, hybrid, 2D blocks, bottleneck analysis, slides
- Person 5: OpenACC/GPU, Slurm templates, plot library, methodology

## Structure
- `src/`      source code (serial/, mpi/, hybrid/, openacc/)
- `tools/`    city generator, compare, scenarios, ensemble
- `tests/`    unit tests and small reference outputs
- `analysis/` plotting library
- `env/`      modules, dependencies, environment info
- `slurm/`    Slurm submission scripts
- `data/`     small input data and download scripts
- `results/`  CSV files, plots and logs
- `docs/`     report, EuroHPC plan, slides

## Build and run
```
source env/modules.sh
make serial mpi hybrid
make check
```
Interface and file formats: `docs/contract.md`. Conventions for Claude Code: `CLAUDE.md`.