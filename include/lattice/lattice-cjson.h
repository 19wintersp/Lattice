#ifndef LATTICE_CJSON_H
#define LATTICE_CJSON_H

#include <cjson/cJSON.h>

#define LATTICE_INSIDE
#include "lattice.h"
#undef LATTICE_INSIDE

LATTICE_IMPLS(_cjson, const cJSON *);

#undef LATTICE_IMPL
#undef LATTICE_IMPLS

#endif // ifndef LATTICE_CJSON_H
