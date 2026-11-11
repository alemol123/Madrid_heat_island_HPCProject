/* test_diag.c - diag_compute() on a grid with known answers (P5).
 * 8x8 interior: the top-left 4x4 block is 40 C, the rest 20 C, ghost cells 999
 * (must be ignored). Expected: peak 40, mean (16*40 + 48*20)/64 = 25,
 * fraction above 30 C = 16/64 = 0.25. A cell exactly at the threshold must
 * not count as "above". */
#include "diag.h"
#include "step.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int check(const char *what, double got, double want)
{
    int ok = fabs(got - want) < 1e-9;
    printf("%s  %-28s got %.6f  want %.6f\n", ok ? "PASS" : "FAIL", what, got, want);
    return ok ? 0 : 1;
}

int main(void)
{
    const int nx = 8, ny = 8;
    size_t npad = (size_t)(nx + 2) * (size_t)(ny + 2);
    float *T = malloc(npad * sizeof(float));
    for (size_t k = 0; k < npad; k++) T[k] = 999.0f;               /* ghosts */
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++)
            T[IDX(i, j, nx)] = (i < 4 && j < 4) ? 40.0f : 20.0f;

    int bad = 0;
    Diag d = diag_compute(T, nx, ny, 30.0f);
    bad += check("peak", d.peak, 40.0);
    bad += check("mean", diag_mean(&d), 25.0);
    bad += check("fraction above 30", diag_frac(&d), 0.25);

    /* Threshold exactly at 40: no cell is strictly above it. */
    Diag e = diag_compute(T, nx, ny, 40.0f);
    bad += check("fraction above 40 (edge)", diag_frac(&e), 0.0);

    free(T);
    printf(bad ? "test_diag: FAILED\n" : "test_diag: all passed\n");
    return bad ? 1 : 0;
}