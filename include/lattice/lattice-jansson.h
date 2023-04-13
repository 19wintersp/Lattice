#ifndef LATTICE_JANSSON_H
#define LATTICE_JANSSON_H

#include <jansson.h>

#define LATTICE_INSIDE
#include "lattice.h"
#undef LATTICE_INSIDE

LATTICE_IMPLS(_jansson, const json_t *);

#undef LATTICE_IMPL
#undef LATTICE_IMPLS

#endif // ifndef LATTICE_JANSSON_H
