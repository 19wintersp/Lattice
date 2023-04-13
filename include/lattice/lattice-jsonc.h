#ifndef LATTICE_JSONC_H
#define LATTICE_JSONC_H

#include <json-c/json_object.h>

#define LATTICE_INSIDE
#include "lattice.h"
#undef LATTICE_INSIDE

LATTICE_IMPLS(_jsonc, const struct json_object *);

#undef LATTICE_IMPL
#undef LATTICE_IMPLS

#endif // ifndef LATTICE_JSONC_H
