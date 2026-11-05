# Physics note: the heat model (P1)

## What we simulate

The surface temperature of a city on a 2D grid of square cells (for example 5 m × 5 m).
Each cell has one land-cover type: road, building, grass/park, tree, water or green roof.
Over a simulated day the sun heats each cell according to its type, heat spreads to the
four neighbours, and each cell loses heat to the air. The result is a temperature map
that shows where the city heats up most (the urban heat island) and how trees, water and
green roofs change it.

## The equation

For every cell, one explicit time step:

    T_new = T + α_c (T̄₄ − T) + S(t) · a_c − k_c (T − T_air)

| Term | Meaning |
|---|---|
| T̄₄ = average of the four neighbours | lateral heat diffusion (5-point stencil) |
| α_c | how strongly the cell mixes with its neighbours per step |
| S(t) = S₀ · max(0, sin(2πt / P)) | solar forcing: zero at night, peak at noon; P = steps per day |
| a_c | how much of the sunlight the surface turns into heat (absorption) |
| k_c (T − T_air) | cooling towards the air: convection, evaporation, evapotranspiration |

Coefficients per type are in `src/params.h`:

| Type | α | a (absorb) | k (cool) | Peak excess a/k (°C) | Reasoning |
|---|---|---|---|---|---|
| road | 0.20 | 0.020 | 0.002 | 10.0 | dark asphalt absorbs most and cannot evaporate |
| building | 0.10 | 0.015 | 0.002 | 7.5 | roofs absorb strongly; walls and air gaps conduct sideways poorly |
| grass/park | 0.20 | 0.008 | 0.006 | 1.3 | lighter surface, evapotranspiration |
| tree | 0.20 | 0.004 | 0.010 | 0.4 | canopy shades the ground and transpires |
| water | 0.25 | 0.003 | 0.012 | 0.25 | reflects, evaporates, stores heat deep |
| green roof | 0.15 | 0.006 | 0.008 | 0.75 | vegetated roof: less absorption, more cooling |

The "peak excess" column is the steady state of an isolated cell under full sun
(set T̄₄ = T): T − T_air = a/k. The values are chosen to give a plausible ordering
(asphalt hottest, water coolest), not calibrated against measurements. Because k is
small for sealed surfaces, they also respond slowly (time constant about 1/k steps).

## Why two buffers

Every new value must be computed from the *old* values of its neighbours. Updating in
place would mix old and new values (the left neighbour would already be updated), so
the result would depend on the loop order. We keep two arrays and swap their pointers
after each step (ping-pong); this also makes every row independent, which is what lets
OpenMP, MPI and the GPU update rows in parallel.

## Stability

Rewrite the update as a weighted sum:

    T_new = (1 − α − k) T + α T̄₄ + k T_air + S a

If α ≥ 0, k ≥ 0 and α + k ≤ 1, all weights are non-negative and sum to 1, so without
the sun the new value lies between the old minimum and maximum (a discrete maximum
principle): errors cannot grow and the scheme cannot oscillate. If α + k > 1 the
weight on T is negative: a cell slightly above its neighbours overshoots to *below*
them the next step, giving non-physical oscillations. Going further, the worst
(checkerboard) mode is multiplied by 1 − 2α − k each step; once 2α + k > 2 its
magnitude exceeds 1 and the error grows every step until the solution blows up
(for k = 0: α > 1). We require the stricter condition α + k ≤ 1, which rules out both.

The well-known limit α ≤ 0.25 belongs to the other way of writing the stencil,
α (sum of four neighbours − 4T); our "average" form is the same thing with α four
times larger, so the limit becomes α ≤ 1. The solver checks α + k ≤ 1 at start-up
and refuses to run otherwise (`check_stability` in `src/common/step.c`).

## Boundaries

The grid has a one-cell ghost frame. By default the ghosts are held at T_air
(Dirichlet: the city is surrounded by air-temperature countryside). An insulated option
(Neumann: ghost = neighbouring cell, no heat leaves) exists for the conservation test.
In MPI, ghost rows between ranks are filled by the halo exchange instead.

## What the model leaves out

- Vertical structure: no building heights, street canyons, shading by buildings or
  heat stored in walls; it is a 2D surface model.
- Radiation exchange between surfaces and night-time long-wave cooling (only the
  linear k (T − T_air) term).
- Wind and advection: heat only moves by diffusion to direct neighbours.
- Humidity, weather, cloud cover, anthropogenic heat (traffic, air conditioning).
- Physical units: α, a, k are per-step model constants, not measured material
  properties; one "day" is P = 1000 steps.

These limits are acceptable because the project compares *relative* effects
(strategy A vs strategy B for trees and green roofs) and studies the HPC behaviour
of a stencil code, which is the same with or without these effects.

## Tests that check the physics (`make test`)

- A uniform field at T_air with no sun stays exactly uniform.
- A single hot cell spreads symmetrically (left-right and up-down mirror images identical).
- With one cell type, no sun, no cooling and insulated edges, total heat is conserved.
- Without sun, values stay between the initial minimum and maximum.
- Coefficients with α + k > 1 are rejected.
