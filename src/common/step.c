/* step.c - heat-update kernel. See step.h for the memory layout. */
#include "step.h"
#include "params.h"

#include <math.h>
#include <stdio.h>

Coeffs default_coeffs(void)
{
    Coeffs c = { ALPHA_TABLE, ABSORB_TABLE, COOL_TABLE, T_AIR };
    return c;
}

int check_stability(const Coeffs *c)
{
    static const char *names[NTYPES] = { "road", "building", "grass", "tree", "water", "green roof" };
    int bad = 0;
    for (int k = 0; k < NTYPES; k++) {
        if (c->alpha[k] < 0.0f || c->cool[k] < 0.0f || c->absorb[k] < 0.0f ||
            c->alpha[k] + c->cool[k] > 1.0f) {
            fprintf(stderr,
                    "unstable coefficients for type %d (%s): alpha=%g cool=%g absorb=%g "
                    "(need all >= 0 and alpha + cool <= 1)\n",
                    k, names[k], c->alpha[k], c->cool[k], c->absorb[k]);
            bad = 1;
        }
    }
    return bad;
}

float solar_at(int t)
{
    const double two_pi = 6.283185307179586;
    double s = sin(two_pi * (double)t / (double)SOLAR_PERIOD);
    return (float)(SOLAR_S0 * (s > 0.0 ? s : 0.0));
}

void apply_boundary(float *T, int nx, int ny, int bc, int sides, float t_air)
{
    const size_t w = (size_t)nx + 2;
    if (sides & SIDE_TOP)          /* ghost row j = -1 */
        for (int i = 0; i < nx; i++)
            T[IDX(i, -1, nx)] = (bc == BC_NEUMANN) ? T[IDX(i, 0, nx)] : t_air;
    if (sides & SIDE_BOTTOM)       /* ghost row j = ny */
        for (int i = 0; i < nx; i++)
            T[IDX(i, ny, nx)] = (bc == BC_NEUMANN) ? T[IDX(i, ny - 1, nx)] : t_air;
    if (sides & SIDE_LEFT)         /* ghost column i = -1 */
        for (int j = 0; j < ny; j++)
            T[(size_t)(j + 1) * w] = (bc == BC_NEUMANN) ? T[IDX(0, j, nx)] : t_air;
    if (sides & SIDE_RIGHT)        /* ghost column i = nx */
        for (int j = 0; j < ny; j++)
            T[(size_t)(j + 1) * w + (size_t)nx + 1] = (bc == BC_NEUMANN) ? T[IDX(nx - 1, j, nx)] : t_air;
}

void step_rows(const float *restrict in, float *restrict out,
               const int8_t *restrict city, int nx, int ny,
               float solar, const Coeffs *restrict c, int j0, int j1)
{
    (void)ny;
    const size_t w = (size_t)nx + 2;
    const float t_air = c->t_air;
    for (int j = j0; j < j1; j++) {
        const float *restrict row = in + (size_t)(j + 1) * w + 1;   /* interior start of row j */
        const float *restrict up  = row - w;
        const float *restrict dn  = row + w;
        float *restrict o         = out + (size_t)(j + 1) * w + 1;
        const int8_t *restrict ct = city + (size_t)j * (size_t)nx;
        for (int i = 0; i < nx; i++) {
            const int k   = ct[i];
            const float t = row[i];
            /* (up+down)+(left+right): mirror images give bitwise-identical sums */
            const float avg = 0.25f * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
            o[i] = t + c->alpha[k] * (avg - t)
                     + solar * c->absorb[k]
                     - c->cool[k] * (t - t_air);
        }
    }
}

void step(const float *restrict in, float *restrict out,
          const int8_t *restrict city, int nx, int ny,
          float solar, const Coeffs *restrict c)
{
    step_rows(in, out, city, nx, ny, solar, c, 0, ny);
}
