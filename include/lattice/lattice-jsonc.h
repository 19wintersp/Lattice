#ifndef LATTICE_JSONC_H
#define LATTICE_JSONC_H

#include <json-c/json_object.h>

#include "lattice.h"

#define LATTICE_IMPL(name, arg) size_t lattice_jsonc ## name( \
	const char *template, const struct json_object *root, lattice_opts, arg)
LATTICE_IMPL(,        size_t (*emit)(const char *));
LATTICE_IMPL(_file,   FILE *file);
LATTICE_IMPL(_buffer, char **buffer);
#undef LATTICE_IMPL

#endif // ifndef LATTICE_JSONC_H
