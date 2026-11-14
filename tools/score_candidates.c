/* score_candidates.c - OpenMP scoring of candidate tree cells ("where would a tree relieve the most heat?").
 *
 * Input: a city file and the temperature field of that city (docs/contract.md formats).
 * For every road cell c (the candidates) the score is
 *
 *      score(c) = sum over cells q with d(c,q) <= R   w(d) * max(0, T(q) - threshold),      w(d) = 1 - d/(R+1)
 *
 * i.e. the heat above the threshold that lies within R cells of c, weighted by closeness. It is a disk
 * convolution of the "excess heat" map, evaluated at the candidates only. The cost is
 * (number of candidates) x (2R+1)^2, which is what OpenMP parallelises (rows of the city, schedule from
 * --schedule so static / dynamic / guided can be compared).
 *
 * Then the N best cells are chosen greedily, best first, skipping any cell within --spacing cells
 * (square distance) of one already chosen, so trees do not pile up in one hotspot. The selected cells
 * become trees (type 3) in the output city file. The result does not depend on the number of threads.
 *
 * Build:  gcc -O3 -march=native -fopenmp -std=c11 -o build/score_candidates tools/score_candidates.c -lm
 * Run:    OMP_NUM_THREADS=8 OMP_PLACES=cores OMP_PROC_BIND=close build/score_candidates \
 *             --city data/district_sol_retiro.city --field build/28548/field.bin \
 *             --radius 15 --top 2000 --spacing 2 --out data/scen/scored_2000.city
 *
 * Options: --city F --field F --out F [--radius R=15] [--top N=1000] [--spacing S=2] [--threshold T=29.0]
 *          [--threads K] [--schedule static|dynamic|guided[,chunk]]=dynamic,4 [--scores F] [--timing-csv F] [--tag S]
 * --scores writes the score of every cell as a field file (0 for non-candidates).
 * --timing-csv appends: tag,threads,schedule,nx,ny,candidates,radius,selected,t_read_s,t_score_s,t_select_s,t_write_s,t_total_s
 */
#define _GNU_SOURCE
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROAD 0
#define TREE 3

typedef struct { float score; int idx; } Cand;

static void die(const char *msg) {
    fprintf(stderr, "score_candidates: %s\n", msg);
    exit(2);
}

static int cmp_cand(const void *a, const void *b) {         /* best score first, ties by position */
    const Cand *x = a, *y = b;
    if (x->score > y->score) return -1;
    if (x->score < y->score) return 1;
    return (x->idx > y->idx) - (x->idx < y->idx);
}

static void *read_file(const char *path, size_t elem, int32_t *nx, int32_t *ny, const char *what) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "score_candidates: cannot open %s file %s\n", what, path); exit(2); }
    int32_t h[2];
    if (fread(h, sizeof(int32_t), 2, f) != 2 || h[0] <= 0 || h[1] <= 0) die("bad header");
    size_t n = (size_t)h[0] * (size_t)h[1];
    void *data = malloc(n * elem);
    if (!data) die("out of memory");
    if (fread(data, elem, n, f) != n) { fprintf(stderr, "score_candidates: %s file %s is too short\n", what, path); exit(2); }
    fclose(f);
    *nx = h[0]; *ny = h[1];
    return data;
}

static void write_file(const char *path, const void *data, size_t elem, int32_t nx, int32_t ny) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "score_candidates: cannot write %s\n", path); exit(2); }
    int32_t h[2] = {nx, ny};
    fwrite(h, sizeof(int32_t), 2, f);
    fwrite(data, elem, (size_t)nx * ny, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *city_path = NULL, *field_path = NULL, *out_path = NULL, *scores_path = NULL, *csv_path = NULL, *tag = "score";
    const char *sched = "dynamic,4";
    int R = 15, top = 1000, spacing = 2, threads = 0;
    float thr = 29.0f;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        int has = i + 1 < argc;
        if (!strcmp(s, "--city") && has) city_path = argv[++i];
        else if (!strcmp(s, "--field") && has) field_path = argv[++i];
        else if (!strcmp(s, "--out") && has) out_path = argv[++i];
        else if (!strcmp(s, "--scores") && has) scores_path = argv[++i];
        else if (!strcmp(s, "--timing-csv") && has) csv_path = argv[++i];
        else if (!strcmp(s, "--tag") && has) tag = argv[++i];
        else if (!strcmp(s, "--schedule") && has) sched = argv[++i];
        else if (!strcmp(s, "--radius") && has) R = atoi(argv[++i]);
        else if (!strcmp(s, "--top") && has) top = atoi(argv[++i]);
        else if (!strcmp(s, "--spacing") && has) spacing = atoi(argv[++i]);
        else if (!strcmp(s, "--threads") && has) threads = atoi(argv[++i]);
        else if (!strcmp(s, "--threshold") && has) thr = (float)atof(argv[++i]);
        else {
            fprintf(stderr, "usage: %s --city F --field F --out F [--radius R] [--top N] [--spacing S] [--threshold T]\n"
                            "          [--threads K] [--schedule static|dynamic|guided[,chunk]] [--scores F] [--timing-csv F] [--tag S]\n", argv[0]);
            return 2;
        }
    }
    if (!city_path || !field_path || !out_path) die("--city, --field and --out are required");
    if (R < 1 || R > 200) die("--radius must be between 1 and 200");
    if (top < 0 || spacing < 0) die("--top and --spacing must be >= 0");

    /* schedule for the parallel loop: "kind[,chunk]" */
    {
        char kind[32];
        int chunk = 0;
        const char *comma = strchr(sched, ',');
        size_t len = comma ? (size_t)(comma - sched) : strlen(sched);
        if (len == 0 || len >= sizeof kind) die("bad --schedule");
        memcpy(kind, sched, len); kind[len] = 0;
        if (comma) chunk = atoi(comma + 1);
        omp_sched_t k;
        if (!strcmp(kind, "static")) k = omp_sched_static;
        else if (!strcmp(kind, "dynamic")) k = omp_sched_dynamic;
        else if (!strcmp(kind, "guided")) k = omp_sched_guided;
        else die("--schedule must be static, dynamic or guided");
        omp_set_schedule(k, chunk);
    }
    if (threads > 0) omp_set_num_threads(threads);

    double t0 = omp_get_wtime();
    int32_t nx, ny, fx, fy;
    int8_t *city = read_file(city_path, 1, &nx, &ny, "city");
    float *field = read_file(field_path, sizeof(float), &fx, &fy, "field");
    if (fx != nx || fy != ny) die("city and field have different sizes");
    double t_read = omp_get_wtime() - t0;

    /* excess heat, padded by R cells of zeros on every side so the window never leaves the array */
    double t1 = omp_get_wtime();
    const long pw = (long)nx + 2L * R, ph = (long)ny + 2L * R;
    float *E = calloc((size_t)(pw * ph), sizeof(float));
    const int kw = 2 * R + 1;
    float *K = calloc((size_t)kw * kw, sizeof(float));
    float *score = calloc((size_t)nx * ny, sizeof(float));
    if (!E || !K || !score) die("out of memory");
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float e = field[(size_t)j * nx + i] - thr;
            E[(size_t)(j + R) * pw + (i + R)] = e > 0.0f ? e : 0.0f;
        }
    for (int dy = -R; dy <= R; dy++)
        for (int dx = -R; dx <= R; dx++) {
            double d = sqrt((double)dx * dx + (double)dy * dy);
            K[(size_t)(dy + R) * kw + (dx + R)] = d <= R ? (float)(1.0 - d / (R + 1.0)) : 0.0f;
        }

    long ncand = 0;
#pragma omp parallel for schedule(runtime) reduction(+ : ncand)
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            if (city[(size_t)j * nx + i] != ROAD) continue;
            ncand++;
            float acc = 0.0f;
            for (int ky = 0; ky < kw; ky++) {
                const float *e = E + (size_t)(j + ky) * pw + i;     /* window row ky, left edge */
                const float *k = K + (size_t)ky * kw;
                float s = 0.0f;
#pragma omp simd reduction(+ : s)
                for (int kx = 0; kx < kw; kx++) s += k[kx] * e[kx];
                acc += s;
            }
            score[(size_t)j * nx + i] = acc;
        }
    }
    double t_score = omp_get_wtime() - t1;

    /* greedy selection with spacing */
    double t2 = omp_get_wtime();
    size_t npos = 0;
    for (long p = 0; p < (long)nx * ny; p++)
        if (city[p] == ROAD && score[p] > 0.0f) npos++;
    Cand *cand = malloc((npos ? npos : 1) * sizeof(Cand));
    if (!cand) die("out of memory");
    size_t c = 0;
    for (long p = 0; p < (long)nx * ny; p++)
        if (city[p] == ROAD && score[p] > 0.0f) { cand[c].score = score[p]; cand[c].idx = (int)p; c++; }
    qsort(cand, npos, sizeof(Cand), cmp_cand);
    uint8_t *blocked = calloc((size_t)nx * ny, 1);
    int8_t *out = malloc((size_t)nx * ny);
    if (!blocked || !out) die("out of memory");
    memcpy(out, city, (size_t)nx * ny);
    int selected = 0;
    for (size_t q = 0; q < npos && selected < top; q++) {
        int p = cand[q].idx;
        if (blocked[p]) continue;
        out[p] = TREE;
        selected++;
        int ci = p % nx, cj = p / nx;
        for (int j = cj - spacing; j <= cj + spacing; j++) {
            if (j < 0 || j >= ny) continue;
            for (int i = ci - spacing; i <= ci + spacing; i++)
                if (i >= 0 && i < nx) blocked[(size_t)j * nx + i] = 1;
        }
    }
    double t_select = omp_get_wtime() - t2;

    double t3 = omp_get_wtime();
    write_file(out_path, out, 1, nx, ny);
    if (scores_path) write_file(scores_path, score, sizeof(float), nx, ny);
    double t_write = omp_get_wtime() - t3;
    double t_total = omp_get_wtime() - t0;

    int nthreads = 1;
#pragma omp parallel
    {
#pragma omp single
        nthreads = omp_get_num_threads();
    }
    printf("SCORE threads=%d schedule=%s grid=%dx%d candidates=%ld positive=%zu radius=%d selected=%d/%d "
           "t_read=%.3f t_score=%.3f t_select=%.3f t_write=%.3f t_total=%.3f\n",
           nthreads, sched, nx, ny, ncand, npos, R, selected, top, t_read, t_score, t_select, t_write, t_total);
    if (selected < top)
        printf("NOTE: only %d cells could be chosen (cells with no heat nearby, or blocked by --spacing, are skipped).\n", selected);

    if (csv_path) {
        int fresh = access(csv_path, F_OK) != 0;
        FILE *g = fopen(csv_path, "a");
        if (g) {
            if (fresh) fprintf(g, "tag,threads,schedule,nx,ny,candidates,radius,selected,t_read_s,t_score_s,t_select_s,t_write_s,t_total_s\n");
            fprintf(g, "%s,%d,\"%s\",%d,%d,%ld,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f\n", tag, nthreads, sched, nx, ny, ncand, R,
                    selected, t_read, t_score, t_select, t_write, t_total);
            fclose(g);
        }
    }
    free(city); free(field); free(E); free(K); free(score); free(cand); free(blocked); free(out);
    return 0;
}
