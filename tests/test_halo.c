/* test_halo.c - P1's test of P3's halo exchange (src/mpi/halo.c).
 *
 * Run on several rank counts, e.g.
 *   mpirun -np 5 build/test_halo          (locally)
 *   srun --mpi=pmix -n 5 build/test_halo  (cluster)
 * or all of them with tests/run_halo_tests.sh.
 *
 * For several global grid sizes (uneven strips, strips of exactly one row) and
 * both boundary conditions, every rank checks that after
 *     halo_exchange()  +  apply_boundary() on its real domain edges
 * its whole padded strip (interior AND ghost frame) is bit-for-bit equal to the
 * same rows of a serial padded grid after apply_boundary() on all four sides.
 * It then checks that one step() on the strip equals the same rows of one step()
 * on the full grid, and repeats the exchange a few times with changed data.
 * Values are small integers + 0.5, exact in float32, so any mix-up of rows,
 * columns, neighbours or message tags shows up as a mismatch.
 * Exit code 0 on all ranks if everything passed, 1 otherwise.
 */
#define _POSIX_C_SOURCE 200809L
#include "halo.h"
#include "params.h"
#include "step.h"
#include "io.h"

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GHOST_INIT (-7.0f)

/* Same rule as heat_mpi.c: first ny % size ranks get one extra row. */
static void strip_of_rank(int ny, int size, int rank, int *j0, int *nrows)
{
    int base = ny / size, extra = ny % size;
    *nrows = base + (rank < extra ? 1 : 0);
    *j0 = rank * base + (rank < extra ? rank : extra);
}

static float value(int i, int gj, int round) { return (float)(100000 * round + 1000 * gj + i) + 0.5f; }

static float *padded(int nx, int ny)
{
    size_t n = (size_t)(nx + 2) * (size_t)(ny + 2);
    float *a = malloc(n * sizeof(float));
    if (!a) { fprintf(stderr, "out of memory\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
    for (size_t k = 0; k < n; k++) a[k] = GHOST_INIT;
    return a;
}

/* Compare local padded rows -1..lny with global padded rows j0-1..j0+lny, all
 * columns -1..nx except the four corners of the ghost rows. Returns the number of differing cells; reports the first one. */
static int compare_frame(const float *L, const float *G, int nx, int lny, int j0,
                         int rank, const char *what)
{
    const size_t w = (size_t)nx + 2;
    int bad = 0;
    for (int lj = -1; lj <= lny; lj++)
        for (int i = -1; i <= nx; i++) {
            /* Corners of the ghost rows are never read by the 5-point stencil and the
             * halo exchange sends only the nx interior columns: skip them. */
            if ((lj == -1 || lj == lny) && (i == -1 || i == nx)) continue;
            float l = L[(size_t)(lj + 1) * w + (size_t)(i + 1)];
            float g = G[(size_t)(j0 + lj + 1) * w + (size_t)(i + 1)];
            if (l != g) {
                if (!bad)
                    fprintf(stderr, "rank %d %s: cell (i=%d, local row %d = global row %d): got %.1f, expected %.1f\n",
                            rank, what, i, lj, j0 + lj, l, g);
                bad++;
            }
        }
    return bad;
}

/* One test case. Returns the number of failures on this rank. */
static int run_case(int nx, int ny, int bc, int rank, int size)
{
    int j0, lny;
    strip_of_rank(ny, size, rank, &j0, &lny);
    const int up = rank > 0 ? rank - 1 : MPI_PROC_NULL;
    const int down = rank < size - 1 ? rank + 1 : MPI_PROC_NULL;
    int edges = SIDE_LEFT | SIDE_RIGHT;
    if (up == MPI_PROC_NULL) edges |= SIDE_TOP;
    if (down == MPI_PROC_NULL) edges |= SIDE_BOTTOM;
    const char *bcname = bc == BC_DIRICHLET ? "dirichlet" : "neumann";
    char what[96];
    int fails = 0;

    float *G = padded(nx, ny), *L = padded(nx, lny);
    float *Gout = padded(nx, ny), *Lout = padded(nx, lny);
    int8_t *gcity = malloc((size_t)nx * ny), *lcity = malloc((size_t)nx * lny);
    synthetic_city_rows(nx, 0, ny, 42, gcity);
    synthetic_city_rows(nx, j0, lny, 42, lcity);
    Coeffs c = default_coeffs();

    for (int round = 0; round < 3; round++) {
        for (int gj = 0; gj < ny; gj++)
            for (int i = 0; i < nx; i++) G[IDX(i, gj, nx)] = value(i, gj, round);
        for (int lj = 0; lj < lny; lj++)
            for (int i = 0; i < nx; i++) L[IDX(i, lj, nx)] = value(i, j0 + lj, round);

        apply_boundary(G, nx, ny, bc, SIDE_ALL, c.t_air);
        halo_exchange(L, nx, lny, up, down, MPI_COMM_WORLD);
        apply_boundary(L, nx, lny, bc, edges, c.t_air);

        snprintf(what, sizeof what, "%dx%d %s round %d: ghost frame", nx, ny, bcname, round);
        fails += compare_frame(L, G, nx, lny, j0, rank, what) ? 1 : 0;

        step(G, Gout, gcity, nx, ny, 0.7f, &c);
        step(L, Lout, lcity, nx, lny, 0.7f, &c);
        int bad = 0;
        for (int lj = 0; lj < lny; lj++)
            for (int i = 0; i < nx; i++)
                if (Lout[IDX(i, lj, nx)] != Gout[IDX(i, j0 + lj, nx)]) bad++;
        if (bad) {
            fprintf(stderr, "rank %d %dx%d %s round %d: one step differs from serial in %d cells\n",
                    rank, nx, ny, bcname, round, bad);
            fails++;
        }
    }
    free(G); free(L); free(Gout); free(Lout); free(gcity); free(lcity);
    return fails;
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* Global sizes: uneven strips, one row per rank, two rows + 1 extra, wider odd grid. */
    int cases[][2] = { { 7, 11 }, { 9, size }, { 5, 2 * size + 1 }, { 37, 3 * size + 2 }, { 1, size + 3 } };
    const int ncases = (int)(sizeof cases / sizeof cases[0]);
    int fails = 0, total = 0;
    for (int k = 0; k < ncases; k++) {
        if (cases[k][1] < size) continue;   /* the solver refuses ny < ranks */
        for (int bc = BC_DIRICHLET; bc <= BC_NEUMANN; bc++) {
            fails += run_case(cases[k][0], cases[k][1], bc, rank, size);
            total += 3;
        }
    }

    int all_fails = 0;
    MPI_Allreduce(&fails, &all_fails, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (rank == 0)
        printf("halo test, %d rank(s): %s (%d checks per rank, %d failure(s) over all ranks)\n",
               size, all_fails ? "FAILED" : "PASS", 2 * total, all_fails);
    MPI_Finalize();
    return all_fails ? 1 : 0;
}
