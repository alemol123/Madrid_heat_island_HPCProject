/* heat_openacc.c - OpenACC (GPU) version of the heat solver (P5).
 *
 *   ./heat_openacc --nx 2048 --ny 2048 --steps 2000 --city synthetic --seed 42 \
 *                  --out results/field.bin --timing results/runs.csv --tag gpu \
 *                  [--acc-mode resident|naive] [--diag results/diag.csv --threshold 29]
 *
 * Same options and output as src/serial/heat.c, plus:
 *   --acc-mode resident (default): copy the city and both grids to the GPU once,
 *                       run every step there, copy the result back once (data residency).
 *   --acc-mode naive:   copy the data to the GPU and back around every step.
 *   --diag FILE:        append peak / mean / fraction above --threshold (default 29 C, same as tools/scenario_metrics.py)
 *                       of the final field to FILE (see src/common/diag.h).
 * The kernels (apply_boundary, step) are the shared ones in src/common/step.c,
 * built with nvc -acc. time_transfer = time spent in CPU<->GPU copies; it is
 * not part of time_compute (docs/contract.md). GPU start-up (acc_init) happens
 * before the timers start: it counts in time_total, not as transfer or compute.
 * Diagnostics are computed after time_total, so they never change the timings.
 */
#define _POSIX_C_SOURCE 200809L
#include "params.h"
#include "step.h"
#include "io.h"
#include "diag.h"

#include <getopt.h>
#include <openacc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

enum { MODE_RESIDENT = 0, MODE_NAIVE = 1 };

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--nx N] [--ny N] [--steps N] [--city synthetic|FILE] [--seed S]\n"
            "          [--out FILE] [--timing CSV] [--tag TAG] [--bc dirichlet|neumann]\n"
            "          [--acc-mode resident|naive] [--diag CSV] [--threshold C]\n", prog);
}

int main(int argc, char **argv)
{
    int nx = 256, ny = 256, steps = 1000, bc = BC_DIRICHLET, mode = MODE_RESIDENT;
    unsigned long long seed = 42;
    const char *city_arg = "synthetic", *out_path = NULL, *csv_path = NULL, *tag = "run";
    const char *diag_path = NULL;
    float threshold = 29.0f;   /* same default as tools/scenario_metrics.py */

    static struct option opts[] = {
        { "nx", required_argument, 0, 'x' },     { "ny", required_argument, 0, 'y' },
        { "steps", required_argument, 0, 's' },  { "city", required_argument, 0, 'c' },
        { "seed", required_argument, 0, 'r' },   { "out", required_argument, 0, 'o' },
        { "timing", required_argument, 0, 't' }, { "tag", required_argument, 0, 'g' },
        { "bc", required_argument, 0, 'b' },     { "acc-mode", required_argument, 0, 'm' },
        { "diag", required_argument, 0, 'd' },   { "threshold", required_argument, 0, 'T' },
        { "help", no_argument, 0, 'h' },
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
        case 'm':
            if (strcmp(optarg, "resident") == 0) mode = MODE_RESIDENT;
            else if (strcmp(optarg, "naive") == 0) mode = MODE_NAIVE;
            else { usage(argv[0]); return 2; }
            break;
        case 'd': diag_path = optarg; break;
        case 'T': threshold = (float)atof(optarg); break;
        default: usage(argv[0]); return 2;
        }
    }

    double t_start = wtime(), t_io = 0.0, t_transfer = 0.0;
    acc_init(acc_device_nvidia);   /* start the GPU now: startup counts in time_total, not as transfer */

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
    if (mode == MODE_RESIDENT) {
        /* Copy once: city, coefficients and both grids stay on the GPU. */
        double tt = wtime();
        #pragma acc enter data copyin(city[0:ncell], a[0:npad], b[0:npad], c)
        t_transfer += wtime() - tt;

        for (int t = 0; t < steps; t++) {
            apply_boundary(a, nx, ny, bc, SIDE_ALL, c.t_air);
            step(a, b, city, nx, ny, solar_at(t), &c);
            float *tmp = a; a = b; b = tmp;      /* a now holds the newest field */
        }

        /* Copy the result back once. */
        tt = wtime();
        #pragma acc update self(a[0:npad])
        #pragma acc exit data delete(city[0:ncell], a[0:npad], b[0:npad], c)
        t_transfer += wtime() - tt;
    } else {
        /* Naive: move everything to the GPU and back around every step. */
        for (int t = 0; t < steps; t++) {
            double tt = wtime();
            #pragma acc enter data copyin(city[0:ncell], a[0:npad], c) create(b[0:npad])
            t_transfer += wtime() - tt;

            apply_boundary(a, nx, ny, bc, SIDE_ALL, c.t_air);
            step(a, b, city, nx, ny, solar_at(t), &c);

            tt = wtime();
            #pragma acc exit data copyout(b[0:npad]) delete(city[0:ncell], a[0:npad], c)
            t_transfer += wtime() - tt;

            float *tmp = a; a = b; b = tmp;
        }
    }
    double t_compute = wtime() - t0 - t_transfer;   /* transfers are not compute */

    double sum = checksum_padded(a, nx, ny);

    if (out_path) {
        t0 = wtime();
        if (write_field_padded(out_path, nx, ny, a)) return 1;
        t_io += wtime() - t0;
    }
    double t_total = wtime() - t_start;

    const char *version = mode == MODE_RESIDENT ? "openacc" : "openacc-naive";
    double updates = (double)ncell * (double)steps;
    printf("heat openacc nx=%d ny=%d steps=%d city=%s seed=%llu kernel=%s mode=%s\n", nx, ny, steps,
           city_arg, seed, step_kernel_name(), mode == MODE_RESIDENT ? "resident" : "naive");
    printf("time_total=%.4f s  time_compute=%.4f s  time_transfer=%.4f s  time_io=%.4f s  %.3f ns/cell-update\n",
           t_total, t_compute, t_transfer, t_io, updates > 0 ? 1e9 * t_compute / updates : 0.0);
    printf("checksum %.10e\n", sum);

    /* ---- diagnostics (after the timers, on the final field, now on the host) ---- */
    if (diag_path) {
        Diag d = diag_compute(a, nx, ny, threshold);
        printf("diag peak=%.3f C  mean=%.3f C  above %.1f C: %.2f%% of cells\n",
               d.peak, diag_mean(&d), d.threshold, 100.0 * diag_frac(&d));
        if (diag_append_csv(diag_path, tag, version, nx, ny, steps, &d)) return 1;
    }

    if (csv_path) {
        const char *job = getenv("SLURM_JOB_ID");
        RunRecord r = { tag, version, nx, ny, steps, 1, 1, 1, 1,
                        t_total, t_compute, 0.0, t_io, t_transfer, sum, GIT_COMMIT, job ? job : "none",
                        NULL /* node: filled in by append_csv */ };
        if (append_csv(csv_path, &r)) return 1;
    }

    free(city); free(a); free(b);
    return 0;
}