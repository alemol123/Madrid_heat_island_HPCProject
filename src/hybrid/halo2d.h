
/* halo2d.h - exchange of ghost rows and columns for 2D MPI decomposition. */
#ifndef HALO2D_H
#define HALO2D_H

#include <mpi.h>

/*
 * Exchange the four edges of a padded local temperature array.
 *
 * nx, ny = interior dimensions owned by this MPI rank.
 *
 * up, down, left, right = neighbouring MPI ranks,
 * or MPI_PROC_NULL at physical domain boundaries.
 *
 * The caller handles physical boundary conditions.
 */
void halo_exchange_2d(
    float *T,
    int nx,
    int ny,
    int up,
    int down,
    int left,
    int right,
    MPI_Comm comm
);

#endif
