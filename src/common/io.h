/* io.h - file formats, synthetic city, checksum, results CSV (docs/contract.md). */
#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <stdio.h>

/* ---- City files: int32 nx, int32 ny, then nx*ny int8, row-major ---- */

/* Read only the header. Returns 0 on success. */
int read_city_header(const char *path, int *nx, int *ny);

/* Read rows j0 .. j0+nrows-1 into city (nrows*nx bytes). Reads by file offset,
 * so each MPI rank can read just its own slice. Returns 0 on success. */
int read_city_rows(const char *path, int nx, int j0, int nrows, int8_t *city);

/* Write a whole city. Returns 0 on success. */
int write_city(const char *path, int nx, int ny, const int8_t *city);

/* ---- Synthetic city ----
 * Minimal stand-in so the solver runs without input files (P2's
 * tools/gen_city.py makes the realistic cities). Each cell's type is a pure
 * function of (i, j, seed), so any rank can generate any slice and the result
 * is identical on every machine and every decomposition. */
int8_t synthetic_cell(int i, int j, uint64_t seed);
void synthetic_city_rows(int nx, int j0, int nrows, uint64_t seed, int8_t *city);

/* ---- Output fields: int32 nx, int32 ny, then nx*ny float32, row-major ---- */

/* Write the interior of a padded (nx+2)x(ny+2) array. Returns 0 on success. */
int write_field_padded(const char *path, int nx, int ny, const float *T);

/* Read a field into an unpadded nx*ny array (caller frees). Returns NULL on error. */
float *read_field(const char *path, int *nx, int *ny);

/* Sum of interior cells of a padded array, accumulated in double. */
double checksum_padded(const float *T, int nx, int ny);

/* ---- Results CSV (one row per run; header written if the file is new) ---- */
typedef struct {
    const char *tag, *version;
    int nx, ny, steps, nodes, ranks, threads, gpus;
    double time_total, time_compute, time_comm, time_io;
    double time_transfer;          /* CPU<->GPU copies (OpenACC); 0 for CPU versions */
    double checksum;
    const char *git_commit, *slurm_jobid;
    const char *node;              /* compute node name; NULL = filled in automatically */
} RunRecord;

/* Name of the node this process runs on: SLURMD_NODENAME, else the hostname.
 * MPI versions: comma-join the distinct names from all ranks before writing. */
const char *node_name(void);

int append_csv(const char *path, const RunRecord *r);

/* Wall-clock seconds (monotonic). */
double wtime(void);

#endif
