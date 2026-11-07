/* bench_kernels.c - P1 serial optimization study: kernel variants, same physics.
 *
 *   ./bench_kernels --nx 2048 --ny 2048 --steps 2000 --reps 3 --seed 42 \
 *                   --timing results/runs.csv [--variants ref,simd,...] [--quiet] [--nocheck]
 *
 * --nocheck skips the untimed reference run (use under perf stat, so the counters
 * cover only the chosen variant).
 *
 * Every variant runs the same time loop as the serial solver (Dirichlet edges,
 * synthetic city, solar cycle) and is checked against the reference kernel
 * step() with the contract's tolerance (max|a-b| <= 1e-5 * max|T|). "exact"
 * means bitwise identical. One CSV row per run, version "serial-opt",
 * tag "opt_<variant>". Exit code 1 if any variant fails the check.
 *
 * Variants (what each one tests):
 *   ref      step() from src/common: per-cell table lookup alpha[k], absorb[k], cool[k]
 *   simd     same loop with #pragma omp simd (vectorization forced, no aliasing checks)
 *   pertype  per step, fold the coefficients into 3 per-type factors:
 *            T' = w[k]*T + a[k]*avg + src[k],  w = 1-a-c, src = S*absorb + c*T_air
 *            (fewer operations per cell, same lookups; rounding differs slightly)
 *   percell  coefficients stored per cell (3 float arrays): no lookup, but
 *            12 more bytes per cell from memory
 *   permute  AVX-512 only: the 6-entry tables sit in one vector register and are
 *            looked up with vpermps (one fast instruction instead of gathers)
 *   double   same as ref in double precision (accuracy/time trade-off)
 */
#define _POSIX_C_SOURCE 200809L
#include "params.h"
#include "step.h"
#include "io.h"

#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__AVX512F__)
#include <immintrin.h>
#endif

#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

/* ------------------------------------------------------------------ kernels */

static void k_simd(const float *restrict in, float *restrict out, const int8_t *restrict city,
                   int nx, int ny, float solar, const Coeffs *restrict c)
{
    const size_t w = (size_t)nx + 2;
    const float t_air = c->t_air;
    for (int j = 0; j < ny; j++) {
        const float *restrict row = in + (size_t)(j + 1) * w + 1;
        const float *restrict up = row - w, *restrict dn = row + w;
        float *restrict o = out + (size_t)(j + 1) * w + 1;
        const int8_t *restrict ct = city + (size_t)j * (size_t)nx;
#pragma omp simd
        for (int i = 0; i < nx; i++) {
            const int k = ct[i];
            const float t = row[i];
            const float avg = 0.25f * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
            o[i] = t + c->alpha[k] * (avg - t) + solar * c->absorb[k] - c->cool[k] * (t - t_air);
        }
    }
}

static void k_pertype(const float *restrict in, float *restrict out, const int8_t *restrict city,
                      int nx, int ny, float solar, const Coeffs *restrict c)
{
    float wt[NTYPES], al[NTYPES], src[NTYPES];
    for (int k = 0; k < NTYPES; k++) {
        wt[k] = 1.0f - c->alpha[k] - c->cool[k];
        al[k] = c->alpha[k];
        src[k] = solar * c->absorb[k] + c->cool[k] * c->t_air;
    }
    const size_t w = (size_t)nx + 2;
    for (int j = 0; j < ny; j++) {
        const float *restrict row = in + (size_t)(j + 1) * w + 1;
        const float *restrict up = row - w, *restrict dn = row + w;
        float *restrict o = out + (size_t)(j + 1) * w + 1;
        const int8_t *restrict ct = city + (size_t)j * (size_t)nx;
#pragma omp simd
        for (int i = 0; i < nx; i++) {
            const int k = ct[i];
            const float avg = 0.25f * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
            o[i] = wt[k] * row[i] + al[k] * avg + src[k];
        }
    }
}

/* Coefficients per cell, built once before the time loop. */
typedef struct { float *alpha, *absorb, *cool; } CellCoeffs;

static void k_percell(const float *restrict in, float *restrict out, const CellCoeffs *cc,
                      int nx, int ny, float solar, float t_air)
{
    const size_t w = (size_t)nx + 2;
    for (int j = 0; j < ny; j++) {
        const float *restrict row = in + (size_t)(j + 1) * w + 1;
        const float *restrict up = row - w, *restrict dn = row + w;
        float *restrict o = out + (size_t)(j + 1) * w + 1;
        const size_t off = (size_t)j * (size_t)nx;
        const float *restrict A = cc->alpha + off, *restrict B = cc->absorb + off, *restrict K = cc->cool + off;
#pragma omp simd
        for (int i = 0; i < nx; i++) {
            const float t = row[i];
            const float avg = 0.25f * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
            o[i] = t + A[i] * (avg - t) + solar * B[i] - K[i] * (t - t_air);
        }
    }
}

#if defined(__AVX512F__)
/* Same arithmetic as ref, in the same order (separate mul/add, no FMA), so the
 * result is bitwise identical; only the coefficient lookup changes. */
static void k_permute(const float *restrict in, float *restrict out, const int8_t *restrict city,
                      int nx, int ny, float solar, const Coeffs *restrict c)
{
    float ta[16] = { 0 }, tb[16] = { 0 }, tk[16] = { 0 };
    for (int k = 0; k < NTYPES; k++) { ta[k] = c->alpha[k]; tb[k] = c->absorb[k]; tk[k] = c->cool[k]; }
    const __m512 A = _mm512_loadu_ps(ta), B = _mm512_loadu_ps(tb), K = _mm512_loadu_ps(tk);
    const __m512 q = _mm512_set1_ps(0.25f), S = _mm512_set1_ps(solar), TA = _mm512_set1_ps(c->t_air);
    const size_t w = (size_t)nx + 2;
    const float t_air = c->t_air;
    for (int j = 0; j < ny; j++) {
        const float *row = in + (size_t)(j + 1) * w + 1;
        const float *up = row - w, *dn = row + w;
        float *o = out + (size_t)(j + 1) * w + 1;
        const int8_t *ct = city + (size_t)j * (size_t)nx;
        int i = 0;
        for (; i + 16 <= nx; i += 16) {
            const __m512i idx = _mm512_cvtepi8_epi32(_mm_loadu_si128((const __m128i *)(ct + i)));
            const __m512 a = _mm512_permutexvar_ps(idx, A);
            const __m512 b = _mm512_permutexvar_ps(idx, B);
            const __m512 k = _mm512_permutexvar_ps(idx, K);
            const __m512 t = _mm512_loadu_ps(row + i);
            const __m512 ud = _mm512_add_ps(_mm512_loadu_ps(up + i), _mm512_loadu_ps(dn + i));
            const __m512 lr = _mm512_add_ps(_mm512_loadu_ps(row + i - 1), _mm512_loadu_ps(row + i + 1));
            const __m512 avg = _mm512_mul_ps(q, _mm512_add_ps(ud, lr));
            __m512 r = _mm512_add_ps(t, _mm512_mul_ps(a, _mm512_sub_ps(avg, t)));
            r = _mm512_add_ps(r, _mm512_mul_ps(S, b));
            r = _mm512_sub_ps(r, _mm512_mul_ps(k, _mm512_sub_ps(t, TA)));
            _mm512_storeu_ps(o + i, r);
        }
        for (; i < nx; i++) {   /* remainder: same formula as ref */
            const int k = ct[i];
            const float t = row[i];
            const float avg = 0.25f * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
            o[i] = t + c->alpha[k] * (avg - t) + solar * c->absorb[k] - c->cool[k] * (t - t_air);
        }
    }
}
#endif

/* Double precision: own buffers and boundary. */
static void k_double(const double *restrict in, double *restrict out, const int8_t *restrict city,
                     int nx, int ny, double solar, const double *al, const double *ab,
                     const double *co, double t_air)
{
    const size_t w = (size_t)nx + 2;
    for (int j = 0; j < ny; j++) {
        const double *restrict row = in + (size_t)(j + 1) * w + 1;
        const double *restrict up = row - w, *restrict dn = row + w;
        double *restrict o = out + (size_t)(j + 1) * w + 1;
        const int8_t *restrict ct = city + (size_t)j * (size_t)nx;
#pragma omp simd
        for (int i = 0; i < nx; i++) {
            const int k = ct[i];
            const double t = row[i];
            const double avg = 0.25 * ((up[i] + dn[i]) + (row[i - 1] + row[i + 1]));
            o[i] = t + al[k] * (avg - t) + solar * ab[k] - co[k] * (t - t_air);
        }
    }
}

/* ------------------------------------------------------------------ driver */

enum { V_REF, V_SIMD, V_PERTYPE, V_PERCELL, V_PERMUTE, V_DOUBLE, NV };
static const char *vname[NV] = { "ref", "simd", "pertype", "percell", "permute", "double" };

static float *alloc_field(size_t npad, float v)
{
    float *a = malloc(npad * sizeof(float));
    if (!a) { fprintf(stderr, "out of memory\n"); exit(1); }
    for (size_t k = 0; k < npad; k++) a[k] = v;
    return a;
}

/* Runs one variant; result (as float, padded) copied into res. Returns compute seconds. */
static double run_variant(int v, int nx, int ny, int steps, const int8_t *city, const Coeffs *c,
                          float *res)
{
    const size_t npad = (size_t)(nx + 2) * (size_t)(ny + 2);
    double t0, t1;

    if (v == V_DOUBLE) {
        double *a = malloc(npad * sizeof(double)), *b = malloc(npad * sizeof(double));
        if (!a || !b) { fprintf(stderr, "out of memory\n"); exit(1); }
        for (size_t k = 0; k < npad; k++) a[k] = b[k] = (double)T_INIT;
        double al[NTYPES], ab[NTYPES], co[NTYPES];
        for (int k = 0; k < NTYPES; k++) { al[k] = c->alpha[k]; ab[k] = c->absorb[k]; co[k] = c->cool[k]; }
        const double t_air = c->t_air;
        const size_t w = (size_t)nx + 2;
        t0 = wtime();
        for (int t = 0; t < steps; t++) {
            for (int i = 0; i < nx + 2; i++) { a[i] = t_air; a[(size_t)(ny + 1) * w + i] = t_air; }
            for (int j = 1; j <= ny; j++) { a[j * w] = t_air; a[j * w + nx + 1] = t_air; }
            k_double(a, b, city, nx, ny, (double)solar_at(t), al, ab, co, t_air);
            double *tmp = a; a = b; b = tmp;
        }
        t1 = wtime();
        for (size_t k = 0; k < npad; k++) res[k] = (float)a[k];
        free(a); free(b);
        return t1 - t0;
    }

    float *a = alloc_field(npad, T_INIT), *b = alloc_field(npad, T_INIT);
    CellCoeffs cc = { 0 };
    if (v == V_PERCELL) {   /* built before the timer: a one-off setup cost */
        size_t n = (size_t)nx * (size_t)ny;
        cc.alpha = malloc(n * sizeof(float)); cc.absorb = malloc(n * sizeof(float)); cc.cool = malloc(n * sizeof(float));
        if (!cc.alpha || !cc.absorb || !cc.cool) { fprintf(stderr, "out of memory\n"); exit(1); }
        for (size_t k = 0; k < n; k++) {
            cc.alpha[k] = c->alpha[city[k]]; cc.absorb[k] = c->absorb[city[k]]; cc.cool[k] = c->cool[city[k]];
        }
    }
    t0 = wtime();
    for (int t = 0; t < steps; t++) {
        apply_boundary(a, nx, ny, BC_DIRICHLET, SIDE_ALL, c->t_air);
        const float s = solar_at(t);
        switch (v) {
        case V_REF:     step(a, b, city, nx, ny, s, c); break;
        case V_SIMD:    k_simd(a, b, city, nx, ny, s, c); break;
        case V_PERTYPE: k_pertype(a, b, city, nx, ny, s, c); break;
        case V_PERCELL: k_percell(a, b, &cc, nx, ny, s, c->t_air); break;
#if defined(__AVX512F__)
        case V_PERMUTE: k_permute(a, b, city, nx, ny, s, c); break;
#endif
        default: break;
        }
        float *tmp = a; a = b; b = tmp;
    }
    t1 = wtime();
    memcpy(res, a, npad * sizeof(float));
    free(a); free(b); free(cc.alpha); free(cc.absorb); free(cc.cool);
    return t1 - t0;
}

int main(int argc, char **argv)
{
    int nx = 1024, ny = -1, steps = 200, reps = 3, quiet = 0, check = 1;
    unsigned long long seed = 42;
    const char *csv = NULL, *list = "ref,simd,pertype,percell,permute,double";
    static struct option opts[] = {
        { "nx", required_argument, 0, 'x' }, { "ny", required_argument, 0, 'y' },
        { "steps", required_argument, 0, 's' }, { "reps", required_argument, 0, 'r' },
        { "seed", required_argument, 0, 'e' }, { "timing", required_argument, 0, 't' },
        { "variants", required_argument, 0, 'v' }, { "quiet", no_argument, 0, 'q' },
        { "nocheck", no_argument, 0, 'n' },
        { 0, 0, 0, 0 }
    };
    int ch;
    while ((ch = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (ch) {
        case 'x': nx = atoi(optarg); break;
        case 'y': ny = atoi(optarg); break;
        case 's': steps = atoi(optarg); break;
        case 'r': reps = atoi(optarg); break;
        case 'e': seed = strtoull(optarg, NULL, 10); break;
        case 't': csv = optarg; break;
        case 'v': list = optarg; break;
        case 'q': quiet = 1; break;
        case 'n': check = 0; break;
        default:
            fprintf(stderr, "usage: %s [--nx N] [--ny N] [--steps N] [--reps R] [--seed S] "
                            "[--timing CSV] [--variants a,b,...] [--quiet] [--nocheck]\n", argv[0]);
            return 2;
        }
    }
    if (ny < 0) ny = nx;
    if (nx < 1 || ny < 1 || steps < 1 || reps < 1) { fprintf(stderr, "bad arguments\n"); return 2; }

    Coeffs c = default_coeffs();
    if (check_stability(&c)) return 1;
    const size_t ncell = (size_t)nx * (size_t)ny, npad = (size_t)(nx + 2) * (size_t)(ny + 2);
    int8_t *city = malloc(ncell);
    float *ref = alloc_field(npad, 0.0f), *res = alloc_field(npad, 0.0f);
    synthetic_city_rows(nx, 0, ny, (uint64_t)seed, city);

    /* Reference result for the correctness check (not timed). */
    if (check) run_variant(V_REF, nx, ny, steps, city, &c, ref);
    double scale = 0.0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) scale = fmax(scale, fabs((double)ref[IDX(i, j, nx)]));
    const double tol = 1e-5 * scale;

    const char *job = getenv("SLURM_JOB_ID");
    printf("bench_kernels nx=%d ny=%d steps=%d reps=%d seed=%llu tolerance=%.3e%s\n",
           nx, ny, steps, reps, seed, tol, check ? "" : " (no check)");
    printf("%-8s %12s %12s %10s %12s  %s\n", "variant", "compute_s", "ns/update", "speedup", "max_diff", "check");

    int failures = 0;
    double t_ref = 0.0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        int v = -1;
        for (int k = 0; k < NV; k++) if (strcmp(tok, vname[k]) == 0) v = k;
        if (v < 0) { fprintf(stderr, "unknown variant '%s'\n", tok); return 2; }
#if !defined(__AVX512F__)
        if (v == V_PERMUTE) { printf("%-8s skipped (needs AVX-512; build with -march=native on an AVX-512 CPU)\n", tok); continue; }
#endif
        double best = 1e30, sum = 0.0;
        for (int r = 0; r < reps; r++) {
            double tc = run_variant(v, nx, ny, steps, city, &c, res);
            sum += tc;
            if (tc < best) best = tc;
            if (csv) {
                char tag[64];
                snprintf(tag, sizeof tag, "opt_%s", vname[v]);
                RunRecord rec = { tag, "serial-opt", nx, ny, steps, 1, 1, 1, 0,
                                  tc, tc, 0.0, 0.0, 0.0, checksum_padded(res, nx, ny),
                                  GIT_COMMIT, job ? job : "none", NULL };
                if (append_csv(csv, &rec)) return 1;
            }
        }
        double maxdiff = 0.0;
        int finite = 1;
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                float x = res[IDX(i, j, nx)];
                if (!isfinite(x)) finite = 0;
                maxdiff = fmax(maxdiff, fabs((double)x - (double)ref[IDX(i, j, nx)]));
            }
        if (!check) maxdiff = 0.0;
        const int ok = finite && (!check || maxdiff <= tol);
        if (!ok) failures++;
        const double mean = sum / reps;
        if (v == V_REF) t_ref = mean;
        if (!quiet || !ok)
            printf("%-8s %12.4f %12.3f %9.2fx %12.3e  %s\n", vname[v], mean, 1e9 * mean / ((double)ncell * steps),
                   t_ref > 0 ? t_ref / mean : 1.0, maxdiff, !check ? "unchecked" : ok ? (maxdiff == 0.0 ? "PASS (exact)" : "PASS") : "FAIL");
    }
    printf("%s\n", !check ? "correctness not checked (--nocheck)" : failures ? "SOME VARIANTS FAILED" : "all variants match the reference");
    free(city); free(ref); free(res);
    return failures ? 1 : 0;
}
