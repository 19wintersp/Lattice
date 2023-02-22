#ifndef LATTICE_CJSON_H
#define LATTICE_CJSON_H

#include <cjson/cJSON.h>

#include "lattice.h"

#define LATTICE_IMPL(name, arg) size_t lattice_cjson ## name( \
	const char *template, const cJSON *root, lattice_opts, arg)
LATTICE_IMPL(,        size_t (*emit)(const char *));
LATTICE_IMPL(_file,   FILE *file);
LATTICE_IMPL(_buffer, char **buffer);
#undef LATTICE_IMPL

#endif // ifndef LATTICE_CJSON_H
