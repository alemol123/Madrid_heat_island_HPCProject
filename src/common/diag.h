/* diag.h - summary diagnostics of a temperature field (P5).
 *
 * Peak temperature, mean temperature and the fraction of cells above a
 * threshold, for P2's scenario study. Works on the interior of a padded
 * (nx+2) x (ny+2) array (see step.h); ghost cells are ignored.
 *
 * Diag keeps sums and counts (not just the mean and fraction), so MPI ranks
 * can combine their local results exactly: see diag_mpi.h.
 * Results go to their own CSV (--diag FILE), so results/runs.csv is unchanged.
 */
#ifndef DIAG_H
#define DIAG_H

typedef struct {
    float peak;           /* highest temperature (C)                 */
    double sum;           /* sum of all temperatures (for the mean)  */
    long long cells;      /* number of cells                         */
    long long above;      /* cells with T > threshold                */
    float threshold;      /* threshold used (C)                      */
} Diag;

/* Diagnostics over the interior of a padded array. Uses an OpenMP reduction
 * when compiled with -fopenmp; plain loop otherwise. */
Diag diag_compute(const float *T, int nx, int ny, float threshold);

static inline double diag_mean(const Diag *d)  { return d->cells ? d->sum / (double)d->cells : 0.0; }
static inline double diag_frac(const Diag *d)  { return d->cells ? (double)d->above / (double)d->cells : 0.0; }

/* Append one row to a diagnostics CSV (header written if the file is new):
 * tag,version,nx,ny,steps,threshold,t_peak,t_mean,frac_above,slurm_jobid
 * Returns 0 on success. */
int diag_append_csv(const char *path, const char *tag, const char *version,
                    int nx, int ny, int steps, const Diag *d);

#endif