/* test_serial.c - unit tests for the shared kernel and I/O. Run with: make test
 * Each test prints PASS/FAIL; the program exits 1 if any test fails. */
#define _POSIX_C_SOURCE 200809L
#include "params.h"
#include "step.h"
#include "io.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, name, ...) do {                                   \
        if (cond) printf("PASS  %s\n", name);                         \
        else { printf("FAIL  %s: ", name); printf(__VA_ARGS__);       \
               printf("\n"); failures++; } } while (0)

static float *new_field(int nx, int ny, float v)
{
    size_t n = (size_t)(nx + 2) * (size_t)(ny + 2);
    float *a = malloc(n * sizeof(float));
    for (size_t k = 0; k < n; k++) a[k] = v;
    return a;
}

/* Run `steps` steps in place; returns the buffer holding the result. */
static float *run(float *a, float *b, const int8_t *city, int nx, int ny, int steps,
                  const Coeffs *c, int bc, int solar_on)
{
    for (int t = 0; t < steps; t++) {
        apply_boundary(a, nx, ny, bc, SIDE_ALL, c->t_air);
        step(a, b, city, nx, ny, solar_on ? solar_at(t) : 0.0f, c);
        float *tmp = a; a = b; b = tmp;
    }
    return a;
}

static double total(const float *T, int nx, int ny) { return checksum_padded(T, nx, ny); }

/* 1. City file round trip: write 64x64, read back, every byte equal. */
static void test_city_roundtrip(void)
{
    const int nx = 64, ny = 64;
    int8_t *c1 = malloc(nx * ny), *c2 = malloc(nx * ny);
    synthetic_city_rows(nx, 0, ny, 42, c1);
    const char *path = "build/test_city_64.bin";
    int rnx = 0, rny = 0;
    int err = write_city(path, nx, ny, c1) || read_city_header(path, &rnx, &rny) ||
              read_city_rows(path, nx, 0, ny, c2);
    CHECK(!err && rnx == nx && rny == ny && memcmp(c1, c2, nx * ny) == 0,
          "city file round trip 64x64", "err=%d header=%dx%d", err, rnx, rny);

    /* reading a slice by offset gives the same rows (what MPI ranks do) */
    int8_t *slice = malloc(10 * nx);
    err = read_city_rows(path, nx, 20, 10, slice);
    CHECK(!err && memcmp(slice, c1 + 20 * nx, 10 * nx) == 0, "city slice read by offset", "err=%d", err);
    free(c1); free(c2); free(slice);
}

/* 2. Synthetic city: same seed -> same city; a slice equals the same rows of the
 *    whole city; a different seed gives a different city; all types valid. */
static void test_synthetic(void)
{
    const int nx = 200, ny = 150;
    int8_t *w = malloc(nx * ny), *w2 = malloc(nx * ny), *s = malloc(nx * 37);
    synthetic_city_rows(nx, 0, ny, 7, w);
    synthetic_city_rows(nx, 0, ny, 7, w2);
    synthetic_city_rows(nx, 51, 37, 7, s);
    int valid = 1, counts[NTYPES] = { 0 };
    for (int k = 0; k < nx * ny; k++) {
        if (w[k] < 0 || w[k] >= NTYPES) valid = 0; else counts[w[k]]++;
    }
    CHECK(memcmp(w, w2, nx * ny) == 0, "synthetic city deterministic", "two calls differ");
    CHECK(memcmp(s, w + 51 * nx, nx * 37) == 0, "synthetic slice matches whole city", "slice differs");
    CHECK(valid && counts[ROAD] > 0 && counts[BUILDING] > 0, "synthetic city has valid types",
          "road=%d building=%d", counts[ROAD], counts[BUILDING]);
    synthetic_city_rows(nx, 0, ny, 8, w2);
    CHECK(memcmp(w, w2, nx * ny) != 0, "different seed gives different city", "seeds 7 and 8 identical");
    free(w); free(w2); free(s);
}

/* 3. Uniform field at T_air, no sun, fixed edges at T_air: stays exactly uniform. */
static void test_uniform(void)
{
    const int nx = 48, ny = 40;
    Coeffs c = default_coeffs();
    int8_t *city = malloc(nx * ny);
    synthetic_city_rows(nx, 0, ny, 42, city);          /* mixed cell types */
    float *a = new_field(nx, ny, c.t_air), *b = new_field(nx, ny, c.t_air);
    float *r = run(a, b, city, nx, ny, 100, &c, BC_DIRICHLET, 0);
    float maxdev = 0.0f;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float d = fabsf(r[IDX(i, j, nx)] - c.t_air);
            if (d > maxdev) maxdev = d;
        }
    CHECK(maxdev == 0.0f, "uniform field stays uniform", "max deviation %g", maxdev);
    free(city); free(a); free(b);
}

/* 4. One hot cell in the centre of an odd grid spreads symmetrically
 *    (left-right and up-down mirror images are identical). */
static void test_symmetry(void)
{
    const int nx = 65, ny = 65;
    Coeffs c = default_coeffs();
    int8_t *city = malloc(nx * ny);
    memset(city, GRASS, nx * ny);
    float *a = new_field(nx, ny, c.t_air), *b = new_field(nx, ny, c.t_air);
    a[IDX(32, 32, nx)] = c.t_air + 100.0f;
    float *r = run(a, b, city, nx, ny, 50, &c, BC_DIRICHLET, 0);
    float worst = 0.0f;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float v = r[IDX(i, j, nx)];
            float lr = fabsf(v - r[IDX(nx - 1 - i, j, nx)]);
            float ud = fabsf(v - r[IDX(i, ny - 1 - j, nx)]);
            if (lr > worst) worst = lr;
            if (ud > worst) worst = ud;
        }
    float spread = r[IDX(33, 32, nx)] - c.t_air;
    CHECK(worst == 0.0f && spread > 0.0f, "hot cell spreads symmetrically",
          "max asymmetry %g, neighbour excess %g", worst, spread);
    free(city); free(a); free(b);
}

/* 5. Heat conservation: one cell type, no sun, no cooling, insulated edges.
 *    Total heat must stay constant (to float rounding). */
static void test_conservation(void)
{
    const int nx = 64, ny = 48;
    Coeffs c = default_coeffs();
    for (int k = 0; k < NTYPES; k++) c.cool[k] = 0.0f;
    int8_t *city = malloc(nx * ny);
    memset(city, ROAD, nx * ny);
    float *a = new_field(nx, ny, 0.0f), *b = new_field(nx, ny, 0.0f);
    srand(1);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++)
            a[IDX(i, j, nx)] = 20.0f + 10.0f * (float)rand() / (float)RAND_MAX;
    double before = total(a, nx, ny);
    float *r = run(a, b, city, nx, ny, 500, &c, BC_NEUMANN, 0);
    double after = total(r, nx, ny);
    double rel = fabs(after - before) / before;
    CHECK(rel < 1e-5, "heat conserved (insulated, no sun, no cooling)",
          "before %.8e after %.8e rel %.2e", before, after, rel);
    free(city); free(a); free(b);
}

/* 6. Maximum principle: without sun every new value stays between the old
 *    minimum and maximum (true only when alpha + cool <= 1). */
static void test_max_principle(void)
{
    const int nx = 50, ny = 50;
    Coeffs c = default_coeffs();
    int8_t *city = malloc(nx * ny);
    synthetic_city_rows(nx, 0, ny, 3, city);
    float *a = new_field(nx, ny, c.t_air), *b = new_field(nx, ny, c.t_air);
    for (int j = 10; j < 20; j++)
        for (int i = 10; i < 20; i++) a[IDX(i, j, nx)] = 60.0f;
    float *r = run(a, b, city, nx, ny, 200, &c, BC_DIRICHLET, 0);
    float lo = 1e30f, hi = -1e30f;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float v = r[IDX(i, j, nx)];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    CHECK(lo >= c.t_air - 1e-4f && hi <= 60.0f, "values stay within initial range",
          "min %g max %g", lo, hi);
    free(city); free(a); free(b);
}

/* 7. Stability check accepts the real table and rejects alpha + cool > 1. */
static void test_stability_check(void)
{
    Coeffs c = default_coeffs();
    CHECK(check_stability(&c) == 0, "params.h coefficients are stable", "rejected");
    c.alpha[ROAD] = 1.2f;
    fprintf(stderr, "(expected message follows) ");
    CHECK(check_stability(&c) != 0, "stability check rejects alpha=1.2", "accepted");
}

/* 8. step_rows over two halves gives exactly the same result as step()
 *    (MPI overlap relies on this). */
static void test_split_rows(void)
{
    const int nx = 40, ny = 30;
    Coeffs c = default_coeffs();
    int8_t *city = malloc(nx * ny);
    synthetic_city_rows(nx, 0, ny, 11, city);
    float *in = new_field(nx, ny, 0.0f), *o1 = new_field(nx, ny, 0.0f), *o2 = new_field(nx, ny, 0.0f);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) in[IDX(i, j, nx)] = 25.0f + 0.1f * (float)((i * 7 + j * 13) % 50);
    apply_boundary(in, nx, ny, BC_DIRICHLET, SIDE_ALL, c.t_air);
    step(in, o1, city, nx, ny, 0.8f, &c);
    step_rows(in, o2, city, nx, ny, 0.8f, &c, 1, ny - 1);   /* interior rows first */
    step_rows(in, o2, city, nx, ny, 0.8f, &c, 0, 1);        /* then boundary rows */
    step_rows(in, o2, city, nx, ny, 0.8f, &c, ny - 1, ny);
    int same = 1;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++)
            if (o1[IDX(i, j, nx)] != o2[IDX(i, j, nx)]) same = 0;
    CHECK(same, "step_rows in pieces == step", "results differ");
    free(city); free(in); free(o1); free(o2);
}

/* 9. Field file round trip. */
static void test_field_roundtrip(void)
{
    const int nx = 33, ny = 17;
    float *a = new_field(nx, ny, -1.0f);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) a[IDX(i, j, nx)] = (float)(j * nx + i) * 0.5f;
    int rnx = 0, rny = 0, err = write_field_padded("build/test_field.bin", nx, ny, a);
    float *r = err ? NULL : read_field("build/test_field.bin", &rnx, &rny);
    int same = r && rnx == nx && rny == ny;
    for (int j = 0; same && j < ny; j++)
        for (int i = 0; i < nx; i++)
            if (r[j * nx + i] != a[IDX(i, j, nx)]) same = 0;
    CHECK(same, "field file round trip (ghosts not written)", "mismatch");
    free(a); free(r);
}

int main(void)
{
    test_city_roundtrip();
    test_synthetic();
    test_uniform();
    test_symmetry();
    test_conservation();
    test_max_principle();
    test_stability_check();
    test_split_rows();
    test_field_roundtrip();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
