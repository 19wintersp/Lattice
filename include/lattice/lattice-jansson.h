#ifndef LATTICE_JANSSON_H
#define LATTICE_JANSSON_H

#include <jansson.h>

#include "lattice.h"

#define LATTICE_IMPL(name, arg) size_t lattice_jansson ## name( \
	const char *template, const json_t *root, lattice_opts, arg)
LATTICE_IMPL(,        size_t (*emit)(const char *));
LATTICE_IMPL(_file,   FILE *file);
LATTICE_IMPL(_buffer, char **buffer);
#undef LATTICE_IMPL

#endif // ifndef LATTICE_JANSSON_H
