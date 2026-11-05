/* heat.c - serial Urban Heat Island solver (the reference every version must match).
 *
 *   ./heat --nx 2048 --ny 2048 --steps 2000 --city synthetic --seed 42 \
 *          --out results/field.bin --timing results/runs.csv --tag baseline
 *
 * Extra options: --bc dirichlet|neumann (default dirichlet, see docs/contract.md),
 *                --city <file> (a city file; nx/ny then come from the file).
 */
#define _POSIX_C_SOURCE 200809L
#include "params.h"
#include "step.h"
#include "io.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--nx N] [--ny N] [--steps N] [--city synthetic|FILE] [--seed S]\n"
            "          [--out FILE] [--timing CSV] [--tag TAG] [--bc dirichlet|neumann]\n", prog);
}

int main(int argc, char **argv)
{
    int nx = 256, ny = 256, steps = 1000, bc = BC_DIRICHLET;
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
            else { usage(argv[0]); return 2; }
            break;
        default: usage(argv[0]); return 2;
        }
    }

    double t_start = wtime(), t_io = 0.0;

    Coeffs c = default_coeffs();
    if (check_stability(&c)) return 1;

    /* ---- city ---- */
    int from_file = strcmp(city_arg, "synthetic") != 0;
    if (from_file) {
        int fnx, fny;
        if (read_city_header(city_arg, &fnx, &fny)) return 1;
        nx = fnx; ny = fny;   /* the file decides the size */
    }
    if (nx < 1 || ny < 1 || steps < 0) { usage(argv[0]); return 2; }

    size_t ncell = (size_t)nx * (size_t)ny;
    size_t npad  = (size_t)(nx + 2) * (size_t)(ny + 2);
    int8_t *city = malloc(ncell);
    float *a = malloc(npad * sizeof(float));
    float *b = malloc(npad * sizeof(float));
    if (!city || !a || !b) { fprintf(stderr, "out of memory (%zu cells)\n", ncell); return 1; }

    double t0 = wtime();
    if (from_file) { if (read_city_rows(city_arg, nx, 0, ny, city)) return 1; }
    else synthetic_city_rows(nx, 0, ny, (uint64_t)seed, city);
    t_io += wtime() - t0;

    for (size_t k = 0; k < npad; k++) { a[k] = T_INIT; b[k] = T_INIT; }

    /* ---- time loop (ping-pong buffers) ---- */
    t0 = wtime();
    for (int t = 0; t < steps; t++) {
        apply_boundary(a, nx, ny, bc, SIDE_ALL, c.t_air);
        step(a, b, city, nx, ny, solar_at(t), &c);
        float *tmp = a; a = b; b = tmp;          /* a now holds the newest field */
    }
    double t_compute = wtime() - t0;

    double sum = checksum_padded(a, nx, ny);

    if (out_path) {
        t0 = wtime();
        if (write_field_padded(out_path, nx, ny, a)) return 1;
        t_io += wtime() - t0;
    }
    double t_total = wtime() - t_start;

    double updates = (double)ncell * (double)steps;
    printf("heat serial nx=%d ny=%d steps=%d city=%s seed=%llu\n", nx, ny, steps, city_arg, seed);
    printf("time_total=%.4f s  time_compute=%.4f s  time_io=%.4f s  %.3f ns/cell-update\n",
           t_total, t_compute, t_io, updates > 0 ? 1e9 * t_compute / updates : 0.0);
    printf("checksum %.10e\n", sum);

    if (csv_path) {
        const char *job = getenv("SLURM_JOB_ID");
        const char *nodes = getenv("SLURM_JOB_NUM_NODES");
        RunRecord r = { tag, "serial", nx, ny, steps, nodes ? atoi(nodes) : 1, 1, 1, 0,
                        t_total, t_compute, 0.0, t_io, 0.0, sum, GIT_COMMIT, job ? job : "none",
                        NULL /* node: filled in by append_csv */ };
        if (append_csv(csv_path, &r)) return 1;
    }

    free(city); free(a); free(b);
    return 0;
}
