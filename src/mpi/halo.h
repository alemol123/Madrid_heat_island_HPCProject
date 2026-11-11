/* halo.h - exchange of ghost rows between neighbouring MPI ranks. */
#ifndef HALO_H
#define HALO_H

#include <mpi.h>

/* Fill the top and bottom ghost rows of a padded (nx+2) x (ny+2) array with the
 * neighbours' edge rows. nx, ny = interior size of THIS rank's array (ny >= 1).
 *
 * up, down = ranks of the neighbours holding the rows just above and below,
 * or MPI_PROC_NULL on a real domain edge. That ghost row is then left untouched,
 * so the caller fills it with apply_boundary(). */
void halo_exchange(float *T, int nx, int ny, int up, int down, MPI_Comm comm);

#endif
