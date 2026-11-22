/*
 * heat_hybrid.c - Hybrid MPI + OpenMP Urban Heat Island solver.
 *
 * The global grid is divided into rectangular 2D blocks using an MPI
 * Cartesian process topology. Each MPI rank uses OpenMP threads to
 * update its local block.
 */
#define _POSIX_C_SOURCE 200809L
#include "params.h"
#include "step.h"
#include "io.h"
#include "halo2d.h"

#include <getopt.h>
#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

#ifndef STEP_TILE_X
#define STEP_TILE_X 256
#endif

#ifndef STEP_TILE_Y
#define STEP_TILE_Y 16
#endif

#define NODE_NAME_LEN 256

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--nx N] [--ny N] [--steps N] [--city synthetic|FILE] [--seed S]\n"
            "          [--out FILE] [--timing CSV] [--tag TAG] [--bc dirichlet|neumann]\n", prog);
}


/* Divide one global dimension among process coordinates.
 * The first 'extra' coordinates receive one additional cell.
 */
static void block_of_coord(int n, int parts, int coord,
                           int *start, int *local_n)
{
    int base = n / parts;
    int extra = n % parts;

    *local_n = base + (coord < extra ? 1 : 0);
    *start = coord * base + (coord < extra ? coord : extra);
}

/* Gather rectangular 2D blocks and reconstruct the global field on rank 0. */
static void gather_field_2d(
    const float *T,
    int nx, int ny,
    int local_nx, int local_ny,
    float *global,
    int rank, int size,
    MPI_Comm cart_comm,
    const int dims[2]
)
{
    /* Pack this rank's interior cells into a contiguous buffer. */
    int local_count = local_nx * local_ny;
    float *sendbuf = malloc((size_t)local_count * sizeof(float));

    if (!sendbuf) {
        fprintf(stderr, "rank %d: out of memory in gather\n", rank);
        MPI_Abort(cart_comm, 1);
    }

    int k = 0;

    for (int j = 0; j < local_ny; j++) {
        for (int i = 0; i < local_nx; i++) {
            sendbuf[k++] = T[IDX(i, j, local_nx)];
        }
    }

    int *counts = NULL;
    int *displs = NULL;
    float *recvbuf = NULL;

    if (rank == 0) {
        counts = malloc((size_t)size * sizeof(int));
        displs = malloc((size_t)size * sizeof(int));
        recvbuf = malloc((size_t)nx * (size_t)ny * sizeof(float));

        if (!counts || !displs || !recvbuf) {
            fprintf(stderr, "rank 0: out of memory in gather\n");
            MPI_Abort(cart_comm, 1);
        }

        int offset = 0;

        for (int r = 0; r < size; r++) {
            int coords[2];
            int start_x, start_y;
            int block_nx, block_ny;

            MPI_Cart_coords(cart_comm, r, 2, coords);

            block_of_coord(nx, dims[1], coords[1], &start_x, &block_nx);
            block_of_coord(ny, dims[0], coords[0], &start_y, &block_ny);

            counts[r] = block_nx * block_ny;
            displs[r] = offset;
            offset += counts[r];
        }
    }

    MPI_Gatherv(
        sendbuf, local_count, MPI_FLOAT,
        recvbuf, counts, displs, MPI_FLOAT,
        0, cart_comm
    );

    /* Rank 0 unpacks the received blocks into global coordinates. */
    if (rank == 0) {
        for (int r = 0; r < size; r++) {
            int coords[2];
            int start_x, start_y;
            int block_nx, block_ny;

            MPI_Cart_coords(cart_comm, r, 2, coords);

            block_of_coord(nx, dims[1], coords[1], &start_x, &block_nx);
            block_of_coord(ny, dims[0], coords[0], &start_y, &block_ny);

            int offset = displs[r];

            for (int j = 0; j < block_ny; j++) {
                for (int i = 0; i < block_nx; i++) {
                    global[IDX(start_x + i, start_y + j, nx)] =
                        recvbuf[offset + j * block_nx + i];
                }
            }
        }
    }

    free(sendbuf);
    free(counts);
    free(displs);
    free(recvbuf);
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
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr, "MPI does not provide the required thread support.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int threads = omp_get_max_threads();

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
    /* ---- 2D Cartesian process grid ---- */

    /* Determine a suitable 2D arrangement of MPI ranks. */
    int dims[2] = {0, 0};
    MPI_Dims_create(size, 2, dims);

    /* The grid cannot have more process partitions than cells. */
    if (dims[0] > ny || dims[1] > nx) {
        if (rank == 0) {
            fprintf(stderr,
                    "Grid %dx%d is too small for process grid %dx%d\n",
                    nx, ny, dims[1], dims[0]);
        }
        return finish(2);
    }

    int periods[2] = {0, 0};
    MPI_Comm cart_comm;

    MPI_Cart_create(
        MPI_COMM_WORLD,
        2,
        dims,
        periods,
        0,
        &cart_comm
    );

    /* Determine this process's Cartesian coordinates. */
    int coords[2];
    int cart_rank;

    MPI_Comm_rank(cart_comm, &cart_rank);
    MPI_Cart_coords(cart_comm, cart_rank, 2, coords);

    /* Determine the rectangular block owned by this process. */
    int i0, j0;
    int local_nx, local_ny;

    block_of_coord(nx, dims[1], coords[1], &i0, &local_nx);
    block_of_coord(ny, dims[0], coords[0], &j0, &local_ny);

    /* Find the four neighbouring processes. */
    int up, down, left, right;

    MPI_Cart_shift(cart_comm, 0, 1, &up, &down);
    MPI_Cart_shift(cart_comm, 1, 1, &left, &right);

    /* Identify which sides are physical domain boundaries. */
    int edge_sides = 0;

    if (up == MPI_PROC_NULL)    edge_sides |= SIDE_TOP;
    if (down == MPI_PROC_NULL)  edge_sides |= SIDE_BOTTOM;
    if (left == MPI_PROC_NULL)  edge_sides |= SIDE_LEFT;
    if (right == MPI_PROC_NULL) edge_sides |= SIDE_RIGHT;

    /* ---- memory (rank 0's output buffer is allocated now, so a failure stops the run early) ---- */
    size_t ncell = (size_t)local_nx * (size_t)local_ny;

    size_t npad = (size_t)(local_nx + 2)
            * (size_t)(local_ny + 2);
    int8_t *city = malloc(ncell);
    float *a = malloc(npad * sizeof(float));
    float *b = malloc(npad * sizeof(float));
    float *global = NULL;
    int want_global = (rank == 0 && out_path != NULL);
    if (want_global) global = malloc((size_t)(nx + 2) * (size_t)(ny + 2) * sizeof(float));
    int no_memory = !city || !a || !b || (want_global && !global);
    if (no_memory) fprintf(stderr, "rank %d: out of memory\n", rank);
    if (any_rank_failed(no_memory)) {
    MPI_Comm_free(&cart_comm);
    return finish(1);
}

    /* ---- city: each rank reads or generates only its own rows ---- */
    double t0 = wtime();
    int city_failed = 0;
    if (from_file) {
    city_failed = read_city_block(
            city_arg, nx, i0, j0, local_nx, local_ny, city
    );
    } else {
        synthetic_city_block(
            nx, i0, j0, local_nx, local_ny, (uint64_t)seed, city
    );
}
    t_io += wtime() - t0;
    if (any_rank_failed(city_failed)) {
    MPI_Comm_free(&cart_comm);
    return finish(1);
}

    #pragma omp parallel for schedule(static)

    for (int j = 0; j < local_ny + 2; j++) {
    size_t row_start = (size_t)j * (size_t)(local_nx + 2);

    for (int i = 0; i < local_nx + 2; i++) {
        size_t k = row_start + (size_t)i;

        a[k] = T_INIT;
        b[k] = T_INIT;
    }
}

    /* ---- time loop (ping-pong buffers) ---- */
    for (int t = 0; t < steps; t++) {
        double t1 = wtime();
        halo_exchange_2d(a, local_nx, local_ny, up, down, left, right, cart_comm);

        double t2 = wtime();
        apply_boundary(a, local_nx, local_ny, bc, edge_sides, c.t_air);
        float solar = solar_at(t);

#ifdef STEP_TILED_2D
        step_rows_tiled_2d(a, b, city, local_nx, local_ny,
                           solar, &c, 0, local_ny, STEP_TILE_X, STEP_TILE_Y);
#elif defined(STEP_TILED)
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < local_ny; j++) {
            step_rows_tiled(a, b, city, local_nx, local_ny,
                            solar, &c, j, j + 1, 256);
        }
#else
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < local_ny; j++) {
            step_rows(a, b, city, local_nx, local_ny,
                      solar, &c, j, j + 1);
        }
#endif
        double t3 = wtime();
        t_comm    += t2 - t1;
        t_compute += t3 - t2;
        float *tmp = a; a = b; b = tmp;          /* a now holds the newest field */
    }

    /* Partial sums add up exactly: float values at this scale fit a double with bits to spare. */
    double local_sum = checksum_padded(a, local_nx, local_ny);
    double sum = 0.0;
    MPI_Reduce(&local_sum, &sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    /* ---- output: gathering counts as I/O, it only happens because of --out ---- */
    if (out_path) {
        t0 = wtime();
        gather_field_2d(a, nx, ny, local_nx, local_ny, global, rank, size, cart_comm, dims);
        int write_failed = (rank == 0) ? write_field_padded(out_path, nx, ny, global) : 0;
        t_io += wtime() - t0;
        if (any_rank_failed(write_failed)) {
    MPI_Comm_free(&cart_comm);
    return finish(1);
}
    }
    double t_total = wtime() - t_start;

    /* A parallel step lasts as long as its slowest rank, so report the maximum. */
    double local_times[4] = { t_total, t_compute, t_comm, t_io }, max_times[4] = { 0 };
    MPI_Reduce(local_times, max_times, 4, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double updates = (double)nx * (double)ny * (double)steps;
        printf("heat hybrid nx=%d ny=%d steps=%d city=%s seed=%llu ranks=%d threads=%d kernel=%s\n",
                nx, ny, steps, city_arg, seed, size, threads, step_kernel_name());
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
            RunRecord r = { tag, "hybrid", nx, ny, steps, nnodes, size, threads, 0,
                max_times[0], max_times[1], max_times[2], max_times[3], 0.0,
                sum, GIT_COMMIT, job ? job : "none", nodes };
            csv_failed = append_csv(csv_path, &r);
        }
        free(nodes);
    }
    if (any_rank_failed(csv_failed)) {
    MPI_Comm_free(&cart_comm);
    return finish(1);
}

    free(city); free(a); free(b); free(global);
    MPI_Comm_free(&cart_comm);
    return finish(0);
}