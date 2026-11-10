/* heat_mpi.c - MPI Urban Heat Island solver with a row-strip decomposition.
 * Same command line and output as src/serial/heat.c, and bitwise-identical results.
 *
 *   srun --mpi=pmix ./heat_mpi --nx 2048 --ny 2048 --steps 2000 --city synthetic --seed 42 \
 *                              --out results/field.bin --timing results/runs.csv --tag baseline
 *
 * Each rank owns a strip of whole rows. Its arrays hold only that strip, plus
 * the usual ghost cells, so step() runs on it exactly as it runs on the full
 * grid in the serial version.
 */
#define _POSIX_C_SOURCE 200809L
#include "params.h"
#include "step.h"
#include "io.h"
#include "halo.h"

#include <getopt.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

#define NODE_NAME_LEN 256

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--nx N] [--ny N] [--steps N] [--city synthetic|FILE] [--seed S]\n"
            "          [--out FILE] [--timing CSV] [--tag TAG] [--bc dirichlet|neumann]\n", prog);
}

/* Rows owned by a rank: the first ny % size ranks take one extra row, so strip
 * sizes differ by at most 1. Strip = rows j0 .. j0+nrows-1 of the full grid. */
static void strip_of_rank(int ny, int size, int rank, int *j0, int *nrows)
{
    int base = ny / size, extra = ny % size;
    *nrows = base + (rank < extra ? 1 : 0);
    *j0 = rank * base + (rank < extra ? rank : extra);
}

/* Collective: tells every rank whether any rank failed, so they all stop together. */
static int any_rank_failed(int failed)
{
    int any;
    MPI_Allreduce(&failed, &any, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);
    return any;
}

static int finish(int code)
{
    MPI_Finalize();
    return code;
}

/* Collect every strip's interior rows into the padded global array on rank 0.
 * The "row" datatype is nx floats with an extent of nx+2 floats, so one count
 * means one row and the ghost cells between rows are skipped automatically. */
static void gather_field(const float *T, int nx, int ny, int local_ny, float *global,
                         int rank, int size)
{
    int *counts = NULL, *displs = NULL;
    if (rank == 0) {
        counts = malloc((size_t)size * sizeof(int));
        displs = malloc((size_t)size * sizeof(int));
        if (!counts || !displs) { fprintf(stderr, "out of memory\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        for (int r = 0; r < size; r++) strip_of_rank(ny, size, r, &displs[r], &counts[r]);
    }

    MPI_Datatype packed, row;
    MPI_Type_contiguous(nx, MPI_FLOAT, &packed);
    MPI_Type_create_resized(packed, 0, (MPI_Aint)(nx + 2) * (MPI_Aint)sizeof(float), &row);
    MPI_Type_commit(&row);

    MPI_Gatherv(&T[IDX(0, 0, nx)], local_ny, row,
                rank == 0 ? &global[IDX(0, 0, nx)] : NULL, counts, displs, row,
                0, MPI_COMM_WORLD);

    MPI_Type_free(&row);
    MPI_Type_free(&packed);
    free(counts);
    free(displs);
}

/* On rank 0, returns the distinct node names comma-joined (caller frees) and
 * stores how many there are in *nnodes. Returns NULL on the other ranks. */
static char *join_node_names(int rank, int size, int *nnodes)
{
    char mine[NODE_NAME_LEN] = { 0 };
    snprintf(mine, sizeof mine, "%s", node_name());

    char *all = NULL, *joined = NULL;
    if (rank == 0) {
        all = malloc((size_t)size * NODE_NAME_LEN);
        joined = malloc((size_t)size * NODE_NAME_LEN + 1);
        if (!all || !joined) { fprintf(stderr, "out of memory\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
    }
    MPI_Gather(mine, NODE_NAME_LEN, MPI_CHAR, all, NODE_NAME_LEN, MPI_CHAR, 0, MPI_COMM_WORLD);
    if (rank != 0) return NULL;

    size_t len = 0;
    joined[0] = '\0';
    *nnodes = 0;
    for (int r = 0; r < size; r++) {
        const char *name = all + (size_t)r * NODE_NAME_LEN;
        int seen = 0;
        for (int q = 0; q < r && !seen; q++)
            seen = strcmp(name, all + (size_t)q * NODE_NAME_LEN) == 0;
        if (seen) continue;
        len += (size_t)sprintf(joined + len, "%s%s", len ? "," : "", name);
        (*nnodes)++;
    }
    free(all);
    return joined;
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int nx = 256, ny = 256, steps = 1000, bc = BC_DIRICHLET, bad_args = 0;
    unsigned long long seed = 42;
    const char *city_arg = "synthetic", *out_path = NULL, *csv_path = NULL, *tag = "run";

    static struct option opts[] = {
        { "nx", required_argument, 0, 'x' },     { "ny", required_argument, 0, 'y' },
        { "steps", required_argument, 0, 's' },  { "city", required_argument, 0, 'c' },
        { "seed", required_argument, 0, 'r' },   { "out", required_argument, 0, 'o' },
        { "timing", required_argument, 0, 't' }, { "tag", required_argument, 0, 'g' },
        { "bc", required_argument, 0, 'b' },     { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };
    opterr = (rank == 0);          /* only rank 0 complains about unknown options */
    int ch;
    while ((ch = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (ch) {
        case 'x': nx = atoi(optarg); break;
        case 'y': ny = atoi(optarg); break;
        case 's': steps = atoi(optarg); break;
        case 'c': city_arg = optarg; break;
        case 'r': seed = strtoull(optarg, NULL, 10); break;
        case 'o': out_path = optarg; break;
        case 't': csv_path = optarg; break;
        case 'g': tag = optarg; break;
        case 'b':
            if (strcmp(optarg, "dirichlet") == 0) bc = BC_DIRICHLET;
            else if (strcmp(optarg, "neumann") == 0) bc = BC_NEUMANN;
            else bad_args = 1;
            break;
        default: bad_args = 1; break;
        }
    }
    if (bad_args) {
        if (rank == 0) usage(argv[0]);
        return finish(2);
    }

    double t_start = wtime(), t_io = 0.0, t_compute = 0.0, t_comm = 0.0;

    Coeffs c = default_coeffs();
    if (check_stability(&c)) return finish(1);

    /* ---- grid size and decomposition ---- */
    int from_file = strcmp(city_arg, "synthetic") != 0;
    if (from_file && any_rank_failed(read_city_header(city_arg, &nx, &ny))) return finish(1);
    if (nx < 1 || ny < 1 || steps < 0) {
        if (rank == 0) usage(argv[0]);
        return finish(2);
    }
    if (ny < size) {
        if (rank == 0) fprintf(stderr, "ny=%d is too small for %d ranks (need ny >= ranks)\n", ny, size);
        return finish(2);
    }

    int j0, local_ny;
    strip_of_rank(ny, size, rank, &j0, &local_ny);
    int up   = (rank > 0)        ? rank - 1 : MPI_PROC_NULL;
    int down = (rank < size - 1) ? rank + 1 : MPI_PROC_NULL;

    /* Halo exchange fills the top/bottom ghosts between ranks; the rest are real edges. */
    int edge_sides = SIDE_LEFT | SIDE_RIGHT;
    if (up == MPI_PROC_NULL)   edge_sides |= SIDE_TOP;
    if (down == MPI_PROC_NULL) edge_sides |= SIDE_BOTTOM;

    /* ---- memory (rank 0's output buffer is allocated now, so a failure stops the run early) ---- */
    size_t ncell = (size_t)nx * (size_t)local_ny;
    size_t npad  = (size_t)(nx + 2) * (size_t)(local_ny + 2);
    int8_t *city = malloc(ncell);
    float *a = malloc(npad * sizeof(float));
    float *b = malloc(npad * sizeof(float));
    float *global = NULL;
    int want_global = (rank == 0 && out_path != NULL);
    if (want_global) global = malloc((size_t)(nx + 2) * (size_t)(ny + 2) * sizeof(float));
    int no_memory = !city || !a || !b || (want_global && !global);
    if (no_memory) fprintf(stderr, "rank %d: out of memory\n", rank);
    if (any_rank_failed(no_memory)) return finish(1);

    /* ---- city: each rank reads or generates only its own rows ---- */
    double t0 = wtime();
    int city_failed = 0;
    if (from_file) city_failed = read_city_rows(city_arg, nx, j0, local_ny, city);
    else synthetic_city_rows(nx, j0, local_ny, (uint64_t)seed, city);
    t_io += wtime() - t0;
    if (any_rank_failed(city_failed)) return finish(1);

    for (size_t k = 0; k < npad; k++) { a[k] = T_INIT; b[k] = T_INIT; }

    /* ---- time loop (ping-pong buffers) ---- */
    for (int t = 0; t < steps; t++) {
        double t1 = wtime();
        halo_exchange(a, nx, local_ny, up, down, MPI_COMM_WORLD);
        double t2 = wtime();
        apply_boundary(a, nx, local_ny, bc, edge_sides, c.t_air);
        step(a, b, city, nx, local_ny, solar_at(t), &c);
        double t3 = wtime();
        t_comm    += t2 - t1;
        t_compute += t3 - t2;
        float *tmp = a; a = b; b = tmp;          /* a now holds the newest field */
    }

    /* Partial sums add up exactly: float values at this scale fit a double with bits to spare. */
    double local_sum = checksum_padded(a, nx, local_ny), sum = 0.0;
    MPI_Reduce(&local_sum, &sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    /* ---- output: gathering counts as I/O, it only happens because of --out ---- */
    if (out_path) {
        t0 = wtime();
        gather_field(a, nx, ny, local_ny, global, rank, size);
        int write_failed = (rank == 0) ? write_field_padded(out_path, nx, ny, global) : 0;
        t_io += wtime() - t0;
        if (any_rank_failed(write_failed)) return finish(1);
    }
    double t_total = wtime() - t_start;

    /* A parallel step lasts as long as its slowest rank, so report the maximum. */
    double local_times[4] = { t_total, t_compute, t_comm, t_io }, max_times[4] = { 0 };
    MPI_Reduce(local_times, max_times, 4, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double updates = (double)nx * (double)ny * (double)steps;
        printf("heat mpi nx=%d ny=%d steps=%d city=%s seed=%llu ranks=%d kernel=%s\n",
               nx, ny, steps, city_arg, seed, size, step_kernel_name());
        printf("time_total=%.4f s  time_compute=%.4f s  time_comm=%.4f s  time_io=%.4f s  %.3f ns/cell-update\n",
               max_times[0], max_times[1], max_times[2], max_times[3],
               updates > 0 ? 1e9 * max_times[1] / updates : 0.0);
        printf("checksum %.10e\n", sum);
    }

    int csv_failed = 0;
    if (csv_path) {
        int nnodes = 0;
        char *nodes = join_node_names(rank, size, &nnodes);
        if (rank == 0) {
            const char *job = getenv("SLURM_JOB_ID");
            RunRecord r = { tag, "mpi", nx, ny, steps, nnodes, size, 1, 0,
                            max_times[0], max_times[1], max_times[2], max_times[3], 0.0,
                            sum, GIT_COMMIT, job ? job : "none", nodes };
            csv_failed = append_csv(csv_path, &r);
        }
        free(nodes);
    }
    if (any_rank_failed(csv_failed)) return finish(1);

    free(city); free(a); free(b); free(global);
    return finish(0);
}
