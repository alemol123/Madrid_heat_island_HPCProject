/* diag_mpi.h - combine diagnostics across MPI ranks (P5).
 * Each rank calls diag_compute() on its own rows, then diag_allreduce()
 * gives every rank the global peak, sum, cell count and count above the
 * threshold. Only MPI versions include this header. */
#ifndef DIAG_MPI_H
#define DIAG_MPI_H

#include <mpi.h>
#include "diag.h"

static inline Diag diag_allreduce(Diag local, MPI_Comm comm)
{
    Diag g = local;
    MPI_Allreduce(&local.peak,  &g.peak,  1, MPI_FLOAT,     MPI_MAX, comm);
    MPI_Allreduce(&local.sum,   &g.sum,   1, MPI_DOUBLE,    MPI_SUM, comm);
    MPI_Allreduce(&local.cells, &g.cells, 1, MPI_LONG_LONG, MPI_SUM, comm);
    MPI_Allreduce(&local.above, &g.above, 1, MPI_LONG_LONG, MPI_SUM, comm);
    return g;
}

#endif