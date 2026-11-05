/* step.h - the heat-update kernel shared by every version.
 *
 * MEMORY LAYOUT (important for MPI, OpenMP and OpenACC):
 *   Temperature arrays are PADDED with one ghost cell on every side:
 *   size (nx+2) x (ny+2), row-major. Interior cell (i, j), 0 <= i < nx,
 *   0 <= j < ny, lives at T[(j+1)*(nx+2) + (i+1)]  ->  use IDX(i, j, nx).
 *   The city array is NOT padded: cell (i, j) at city[j*nx + i].
 *
 *   step() only reads ghost cells and only writes interior cells. Whoever
 *   calls it fills the ghosts first:
 *     - serial: apply_boundary() on all four sides,
 *     - MPI: halo exchange fills top/bottom ghost rows from neighbours;
 *       apply_boundary() fills only the sides that are a real domain edge.
 */
#ifndef STEP_H
#define STEP_H

#include <stdint.h>

#define IDX(i, j, nx) ((size_t)((j) + 1) * (size_t)((nx) + 2) + (size_t)((i) + 1))

typedef struct {
    float alpha[6];
    float absorb[6];
    float cool[6];
    float t_air;
} Coeffs;

/* Boundary conditions for the ghost cells. */
enum { BC_DIRICHLET = 0,   /* ghost = T_air (fixed edge; the default)     */
       BC_NEUMANN   = 1 }; /* ghost = neighbouring interior cell (insulated) */

/* Which sides apply_boundary() touches (bitmask). */
enum { SIDE_TOP = 1, SIDE_BOTTOM = 2, SIDE_LEFT = 4, SIDE_RIGHT = 8, SIDE_ALL = 15 };

/* Coefficients from params.h. */
Coeffs default_coeffs(void);

/* Returns 0 if alpha + cool <= 1 and all coefficients >= 0 for every type,
 * otherwise prints the offending type to stderr and returns 1. */
int check_stability(const Coeffs *c);

/* Solar forcing at step t: S0 * max(0, sin(2*pi*t/period)). */
float solar_at(int t);

/* Fill ghost cells on the given sides. nx, ny = interior size of this array. */
void apply_boundary(float *T, int nx, int ny, int bc, int sides, float t_air);

/* One explicit step for interior rows j0 <= j < j1 (all columns).
 * Split by rows so the MPI overlap version can update interior rows while the
 * halo is in flight and the boundary rows afterwards. */
void step_rows(const float *restrict in, float *restrict out,
               const int8_t *restrict city, int nx, int ny,
               float solar, const Coeffs *restrict c, int j0, int j1);

/* One explicit step for the whole interior: step_rows(..., 0, ny). */
void step(const float *restrict in, float *restrict out,
          const int8_t *restrict city, int nx, int ny,
          float solar, const Coeffs *restrict c);

#endif
