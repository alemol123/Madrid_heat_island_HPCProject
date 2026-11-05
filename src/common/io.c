/* io.c - file formats, synthetic city, checksum, CSV. Assumes a little-endian
 * machine (x86-64, ARM: every node we use). */
#define _POSIX_C_SOURCE 200809L
#include "io.h"
#include "step.h"
#include "params.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------- city files ---------------- */

int read_city_header(const char *path, int *nx, int *ny)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    int32_t h[2];
    int ok = fread(h, sizeof(int32_t), 2, f) == 2;
    fclose(f);
    if (!ok || h[0] <= 0 || h[1] <= 0) {
        fprintf(stderr, "%s: bad city header\n", path);
        return 1;
    }
    *nx = h[0]; *ny = h[1];
    return 0;
}

int read_city_rows(const char *path, int nx, int j0, int nrows, int8_t *city)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    long off = 2L * (long)sizeof(int32_t) + (long)j0 * (long)nx;
    size_t n = (size_t)nrows * (size_t)nx;
    int ok = fseek(f, off, SEEK_SET) == 0 && fread(city, 1, n, f) == n;
    fclose(f);
    if (!ok) { fprintf(stderr, "%s: short read of city rows %d..%d\n", path, j0, j0 + nrows - 1); return 1; }
    for (size_t k = 0; k < n; k++)
        if (city[k] < 0 || city[k] >= NTYPES) {
            fprintf(stderr, "%s: invalid cell type %d\n", path, city[k]);
            return 1;
        }
    return 0;
}

int write_city(const char *path, int nx, int ny, const int8_t *city)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    int32_t h[2] = { nx, ny };
    size_t n = (size_t)nx * (size_t)ny;
    int ok = fwrite(h, sizeof(int32_t), 2, f) == 2 && fwrite(city, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : 1;
}

/* ---------------- synthetic city ---------------- */

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* Uniform in [0, 1) from three integers. */
static double hash01(uint64_t seed, uint64_t a, uint64_t b)
{
    uint64_t h = splitmix64(seed ^ splitmix64(a * 0x632BE59BD9B4E019ULL ^ splitmix64(b + 0x1234567ULL)));
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

#define BLOCK 32   /* cells per city block (160 m at 5 m cells) */
#define ROADW 4    /* road width in cells */

int8_t synthetic_cell(int i, int j, uint64_t seed)
{
    int bx = i / BLOCK, by = j / BLOCK;
    int li = i % BLOCK, lj = j % BLOCK;

    if (li < ROADW || lj < ROADW) {                     /* road band */
        int edge = (li == ROADW - 1) || (lj == ROADW - 1);
        if (edge && hash01(seed, (uint64_t)i, (uint64_t)j + 7777777ULL) < 0.15)
            return TREE;                                 /* street tree */
        return ROAD;
    }
    double u = hash01(seed, (uint64_t)bx + 1000003ULL, (uint64_t)by);
    if (u < 0.10)                                        /* park block */
        return hash01(seed, (uint64_t)i, (uint64_t)j) < 0.25 ? TREE : GRASS;
    if (u < 0.13)                                        /* pond / reservoir */
        return WATER;
    /* building block with a 1-cell pavement ring and a central courtyard */
    if (li == ROADW || lj == ROADW || li == BLOCK - 1 || lj == BLOCK - 1)
        return ROAD;
    int c0 = (ROADW + BLOCK) / 2 - 3, c1 = (ROADW + BLOCK) / 2 + 3;
    if (li >= c0 && li < c1 && lj >= c0 && lj < c1)
        return GRASS;
    return BUILDING;
}

void synthetic_city_rows(int nx, int j0, int nrows, uint64_t seed, int8_t *city)
{
    for (int r = 0; r < nrows; r++)
        for (int i = 0; i < nx; i++)
            city[(size_t)r * (size_t)nx + (size_t)i] = synthetic_cell(i, j0 + r, seed);
}

/* ---------------- output fields ---------------- */

int write_field_padded(const char *path, int nx, int ny, const float *T)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    int32_t h[2] = { nx, ny };
    int ok = fwrite(h, sizeof(int32_t), 2, f) == 2;
    for (int j = 0; ok && j < ny; j++)
        ok = fwrite(&T[IDX(0, j, nx)], sizeof(float), (size_t)nx, f) == (size_t)nx;
    ok = (fclose(f) == 0) && ok;
    if (!ok) fprintf(stderr, "%s: write failed\n", path);
    return ok ? 0 : 1;
}

float *read_field(const char *path, int *nx, int *ny)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    int32_t h[2];
    if (fread(h, sizeof(int32_t), 2, f) != 2 || h[0] <= 0 || h[1] <= 0) { fclose(f); return NULL; }
    size_t n = (size_t)h[0] * (size_t)h[1];
    float *a = malloc(n * sizeof(float));
    if (!a || fread(a, sizeof(float), n, f) != n) { free(a); fclose(f); return NULL; }
    fclose(f);
    *nx = h[0]; *ny = h[1];
    return a;
}

double checksum_padded(const float *T, int nx, int ny)
{
    double s = 0.0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++)
            s += (double)T[IDX(i, j, nx)];
    return s;
}

/* ---------------- CSV ---------------- */

int append_csv(const char *path, const RunRecord *r)
{
    struct stat st;
    int need_header = (stat(path, &st) != 0) || st.st_size == 0;
    FILE *f = fopen(path, "a");
    if (!f) { perror(path); return 1; }
    if (need_header)
        fprintf(f, "tag,version,nx,ny,steps,nodes,ranks,threads,gpus,"
                   "time_total,time_compute,time_comm,time_io,time_transfer,checksum,git_commit,slurm_jobid,node\n");
    /* node may hold several comma-separated names (MPI): quote it for CSV */
    fprintf(f, "%s,%s,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.10e,%s,%s,\"%s\"\n",
            r->tag, r->version, r->nx, r->ny, r->steps, r->nodes, r->ranks, r->threads, r->gpus,
            r->time_total, r->time_compute, r->time_comm, r->time_io, r->time_transfer, r->checksum,
            r->git_commit, r->slurm_jobid, r->node ? r->node : node_name());
    return fclose(f) == 0 ? 0 : 1;
}

const char *node_name(void)
{
    static char buf[256];
    const char *s = getenv("SLURMD_NODENAME");
    if (s && *s) return s;
    if (gethostname(buf, sizeof buf - 1) != 0) return "unknown";
    buf[sizeof buf - 1] = '\0';
    char *dot = strchr(buf, '.');          /* short name: haskell, julia, fortran */
    if (dot) *dot = '\0';
    return buf;
}

double wtime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
