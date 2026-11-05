/* params.h - shared physical constants for every version (serial, MPI, hybrid, OpenACC).
 *
 * This is the ONLY place these numbers are defined. Change them only with the
 * whole team's agreement, because every version is compared against serial.
 *
 * Units: temperature in degrees C, time in solver steps (dimensionless model).
 *
 * Heat update per step (docs/contract.md):
 *   T_new = T + alpha_c*(avg4 - T) + solar(t)*absorb_c - cool_c*(T - T_air)
 *
 * Stability: T_new = (1 - alpha - cool)*T + alpha*avg4 + cool*T_air + source.
 * If alpha + cool <= 1 every weight is >= 0, so T_new is a weighted average of
 * old values plus a source: no oscillation, no blow-up. check_stability() in
 * step.c enforces this at startup.
 *
 * Steady state of one isolated cell type under full sun (avg4 = T):
 *   T - T_air = absorb / cool   (shown in the comments below)
 */
#ifndef PARAMS_H
#define PARAMS_H

#define NTYPES 6

enum { ROAD = 0, BUILDING = 1, GRASS = 2, TREE = 3, WATER = 4, GREEN_ROOF = 5 };

#define T_AIR        25.0f   /* ambient air temperature, C */
#define T_INIT       25.0f   /* initial temperature everywhere, C */
#define SOLAR_S0     1.0f    /* peak solar forcing (scales absorb) */
#define SOLAR_PERIOD 1000    /* steps per simulated day */

/*                         road   building grass  tree   water  green roof */
/* alpha: lateral diffusion share per step. Water and grass mix well; buildings
 * conduct sideways poorly (walls, air gaps). */
#define ALPHA_TABLE  { 0.20f, 0.10f,  0.20f, 0.20f, 0.25f, 0.15f }
/* absorb: heating per step at full sun. Dark asphalt absorbs most; tree canopy
 * shades the surface; water reflects and stores heat deep. */
#define ABSORB_TABLE { 0.020f, 0.015f, 0.008f, 0.004f, 0.003f, 0.006f }
/* cool: relaxation towards T_air per step. Evapotranspiration (trees, grass,
 * green roofs) and evaporation (water) cool strongly; sealed surfaces barely. */
#define COOL_TABLE   { 0.002f, 0.002f, 0.006f, 0.010f, 0.012f, 0.008f }
/* Resulting peak excess absorb/cool (C above air):
 *   road 10.0, building 7.5, grass 1.3, tree 0.4, water 0.25, green roof 0.75 */

/* Diagnostics threshold for "hot area" (P5's diagnostics, P2's scenarios). */
#define T_HOT_THRESHOLD 30.0f

#endif
