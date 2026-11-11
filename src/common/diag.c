/* diag.c - summary diagnostics of a temperature field (P5). See diag.h. */
#include "diag.h"
#include "step.h"     /* IDX(): index of interior cell (i, j) in a padded array */

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

Diag diag_compute(const float *T, int nx, int ny, float threshold)
{
    float peak = -FLT_MAX;
    double sum = 0.0;
    long long above = 0;

    /* Each thread keeps its own peak, sum and count; OpenMP combines them at
     * the end (max for the peak, + for the sum and count). */
#ifdef _OPENMP
    #pragma omp parallel for reduction(max:peak) reduction(+:sum, above)
#endif
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            const float t = T[IDX(i, j, nx)];
            if (t > peak) peak = t;
            sum += t;
            if (t > threshold) above++;
        }
    }

    Diag d = { peak, sum, (long long)nx * (long long)ny, above, threshold };
    return d;
}

int diag_append_csv(const char *path, const char *tag, const char *version,
                    int nx, int ny, int steps, const Diag *d)
{
    FILE *probe = fopen(path, "r");
    int is_new = (probe == NULL);
    if (probe) fclose(probe);

    FILE *f = fopen(path, "a");
    if (!f) { perror(path); return 1; }
    if (is_new)
        fprintf(f, "tag,version,nx,ny,steps,threshold,t_peak,t_mean,frac_above,slurm_jobid\n");
    const char *job = getenv("SLURM_JOB_ID");
    fprintf(f, "%s,%s,%d,%d,%d,%.2f,%.4f,%.4f,%.6f,%s\n",
            tag, version, nx, ny, steps, d->threshold, d->peak, diag_mean(d), diag_frac(d),
            job ? job : "none");
    fclose(f);
    return 0;
}