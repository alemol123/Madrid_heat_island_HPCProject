/* halo.c - see halo.h. */
#include "halo.h"
#include "step.h"

#include <stddef.h>

enum { TAG_GOING_UP = 100, TAG_GOING_DOWN = 101 };

void halo_exchange(float *T, int nx, int ny, int up, int down, MPI_Comm comm)
{
    /* My first row goes up; my bottom ghost row arrives from below. */
    MPI_Sendrecv(&T[IDX(0, 0, nx)],  nx, MPI_FLOAT, up,   TAG_GOING_UP,
                 &T[IDX(0, ny, nx)], nx, MPI_FLOAT, down, TAG_GOING_UP,
                 comm, MPI_STATUS_IGNORE);

    /* My last row goes down; my top ghost row arrives from above. */
    MPI_Sendrecv(&T[IDX(0, ny - 1, nx)], nx, MPI_FLOAT, down, TAG_GOING_DOWN,
                 &T[IDX(0, -1, nx)],     nx, MPI_FLOAT, up,   TAG_GOING_DOWN,
                 comm, MPI_STATUS_IGNORE);
}

void halo_begin(float *T, int nx, int ny, int up, int down, MPI_Comm comm, MPI_Request req[4])
{
    /* Receives first, so the messages can land straight in the ghost rows. */
    MPI_Irecv(&T[IDX(0, ny, nx)],     nx, MPI_FLOAT, down, TAG_GOING_UP,   comm, &req[0]);
    MPI_Irecv(&T[IDX(0, -1, nx)],     nx, MPI_FLOAT, up,   TAG_GOING_DOWN, comm, &req[1]);
    MPI_Isend(&T[IDX(0, 0, nx)],      nx, MPI_FLOAT, up,   TAG_GOING_UP,   comm, &req[2]);
    MPI_Isend(&T[IDX(0, ny - 1, nx)], nx, MPI_FLOAT, down, TAG_GOING_DOWN, comm, &req[3]);
}

void halo_end(MPI_Request req[4])
{
    MPI_Waitall(4, req, MPI_STATUSES_IGNORE);
}
