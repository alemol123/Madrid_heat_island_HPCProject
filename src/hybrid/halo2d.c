
/* halo2d.c - exchange of ghost rows and columns for 2D MPI decomposition. */

#include "halo2d.h"
#include "step.h"

#include <stddef.h>

enum {
    TAG_GOING_UP = 200,
    TAG_GOING_DOWN = 201,
    TAG_GOING_LEFT = 202,
    TAG_GOING_RIGHT = 203
};

void halo_exchange_2d(
    float *T,
    int nx,
    int ny,
    int up,
    int down,
    int left,
    int right,
    MPI_Comm comm)
{
    /* Exchange top and bottom rows. */

    MPI_Sendrecv(
        &T[IDX(0, 0, nx)], nx, MPI_FLOAT,
        up, TAG_GOING_UP,
        &T[IDX(0, ny, nx)], nx, MPI_FLOAT,
        down, TAG_GOING_UP,
        comm, MPI_STATUS_IGNORE
    );

    MPI_Sendrecv(
        &T[IDX(0, ny - 1, nx)], nx, MPI_FLOAT,
        down, TAG_GOING_DOWN,
        &T[IDX(0, -1, nx)], nx, MPI_FLOAT,
        up, TAG_GOING_DOWN,
        comm, MPI_STATUS_IGNORE
    );

    /* Create a datatype describing one interior column. */

    MPI_Datatype column;
    MPI_Type_vector(ny, 1, nx + 2, MPI_FLOAT, &column);
    MPI_Type_commit(&column);

    /* Exchange left and right columns. */

    MPI_Sendrecv(
        &T[IDX(0, 0, nx)], 1, column,
        left, TAG_GOING_LEFT,
        &T[IDX(nx, 0, nx)], 1, column,
        right, TAG_GOING_LEFT,
        comm, MPI_STATUS_IGNORE
    );

    MPI_Sendrecv(
        &T[IDX(nx - 1, 0, nx)], 1, column,
        right, TAG_GOING_RIGHT,
        &T[IDX(-1, 0, nx)], 1, column,
        left, TAG_GOING_RIGHT,
        comm, MPI_STATUS_IGNORE
    );

    MPI_Type_free(&column);
}
