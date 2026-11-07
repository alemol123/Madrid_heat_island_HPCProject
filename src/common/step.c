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

/* Kernel selection (compile time):
 *   AVX-512 available (e.g. -march=native on the cluster's Zen 5 nodes): the six
 *   per-type coefficients are kept in vector registers and looked up with one
 *   permute instruction (vpermps) per coefficient, 16 cells at a time. This was
 *   3.9-4.5x faster than the scalar table lookup on fortran (docs/p1_results.md).
 *   The arithmetic is the same operations in the same order (separate multiply
 *   and add, no FMA), so results are bitwise identical to the scalar kernel.
 *   Otherwise (Mac, older CPUs, -DSTEP_SCALAR, the NVIDIA compiler): scalar loop. */
#if defined(__AVX512F__) && !defined(STEP_SCALAR) && !defined(__NVCOMPILER)
#define STEP_AVX512 1
#include <immintrin.h>
#endif

const char *step_kernel_name(void)
{
#ifdef STEP_AVX512
    return "avx512-permute";
#else
    return "scalar-lookup";
#endif
}

/* One row, cells i0 .. nx-1, scalar table lookup (also the AVX-512 remainder). */
static inline void row_scalar(const float *restrict row, const float *restrict up,
                              const float *restrict dn, float *restrict o,
                              const int8_t *restrict ct, int i0, int nx,
                              float solar, const Coeffs *restrict c)
{
    const float t_air = c->t_air;
    for (int i = i0; i < nx; i++) {
        const int k   = ct[i];
        const float t = row[i];
        /* (up+down)+(left+right): mirror images give bitwise-identical sums */
        const float avg = 0.25f * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
        o[i] = t + c->alpha[k] * (avg - t)
                 + solar * c->absorb[k]
                 - c->cool[k] * (t - t_air);
    }
}

void step_rows(const float *restrict in, float *restrict out,
               const int8_t *restrict city, int nx, int ny,
               float solar, const Coeffs *restrict c, int j0, int j1)
{
    (void)ny;
    const size_t w = (size_t)nx + 2;
#ifdef STEP_AVX512
    float ta[16] = { 0 }, tb[16] = { 0 }, tk[16] = { 0 };
    for (int k = 0; k < NTYPES; k++) { ta[k] = c->alpha[k]; tb[k] = c->absorb[k]; tk[k] = c->cool[k]; }
    const __m512 A = _mm512_loadu_ps(ta), B = _mm512_loadu_ps(tb), K = _mm512_loadu_ps(tk);
    const __m512 Q = _mm512_set1_ps(0.25f), S = _mm512_set1_ps(solar), TA = _mm512_set1_ps(c->t_air);
#endif
    for (int j = j0; j < j1; j++) {
        const float *restrict row = in + (size_t)(j + 1) * w + 1;   /* interior start of row j */
        const float *restrict up  = row - w;
        const float *restrict dn  = row + w;
        float *restrict o         = out + (size_t)(j + 1) * w + 1;
        const int8_t *restrict ct = city + (size_t)j * (size_t)nx;
        int i = 0;
#ifdef STEP_AVX512
        for (; i + 16 <= nx; i += 16) {
            const __m512i idx = _mm512_cvtepi8_epi32(_mm_loadu_si128((const __m128i *)(ct + i)));
            const __m512 a  = _mm512_permutexvar_ps(idx, A);
            const __m512 b  = _mm512_permutexvar_ps(idx, B);
            const __m512 k  = _mm512_permutexvar_ps(idx, K);
            const __m512 t  = _mm512_loadu_ps(row + i);
            const __m512 ud = _mm512_add_ps(_mm512_loadu_ps(up + i), _mm512_loadu_ps(dn + i));
            const __m512 lr = _mm512_add_ps(_mm512_loadu_ps(row + i - 1), _mm512_loadu_ps(row + i + 1));
            const __m512 avg = _mm512_mul_ps(Q, _mm512_add_ps(ud, lr));
            __m512 r = _mm512_add_ps(t, _mm512_mul_ps(a, _mm512_sub_ps(avg, t)));
            r = _mm512_add_ps(r, _mm512_mul_ps(S, b));
            r = _mm512_sub_ps(r, _mm512_mul_ps(k, _mm512_sub_ps(t, TA)));
            _mm512_storeu_ps(o + i, r);
        }
#endif
        row_scalar(row, up, dn, o, ct, i, nx, solar, c);
    }
}

void step(const float *restrict in, float *restrict out,
          const int8_t *restrict city, int nx, int ny,
          float solar, const Coeffs *restrict c)
{
    step_rows(in, out, city, nx, ny, solar, c, 0, ny);
}
