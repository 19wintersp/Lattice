#ifndef LATTICE_H
#define LATTICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define LATTICE_VERSION_MAJOR 0
#define LATTICE_VERSION_MINOR 1
#define LATTICE_VERSION_PATCH 0
#define LATTICE_VERSION_STR   "0.1.0"

typedef enum lattice_type {
	LATTICE_TYPE_NULL,
	LATTICE_TYPE_BOOLEAN,
	LATTICE_TYPE_NUMBER,
	LATTICE_TYPE_STRING,
	LATTICE_TYPE_ARRAY,
	LATTICE_TYPE_OBJECT,
} lattice_type;

typedef union lattice_index {
	size_t array;
	const char *object;
} lattice_index;

typedef union lattice_value {
	bool boolean;
	double number;
	const char *string;
} lattice_value;

typedef struct lattice_iface {
	void *(*parse)(const char *, size_t);
	char *(*print)(const void *);
	void (*free)(void *);
	void *(*create)(lattice_type, lattice_value);
	void *(*clone)(const void *);
	lattice_type (*type)(const void *);
	lattice_value (*value)(const void *);
	size_t (*length)(const void *);
	void *(*get)(const void *, lattice_index);
	void (*add)(void *, const char *, void *);
} lattice_iface;

typedef struct lattice_opts {
	const char * const *search;
	char *(*resolve)(const char *);
	char *(*escape)(const char *);
	bool ignore_emit_zero;
} lattice_opts;

typedef enum lattice_error_code {
	LATTICE_UNKNOWN_ERROR,
	LATTICE_ALLOC_ERROR,
	LATTICE_IO_ERROR,
	LATTICE_OPTS_ERROR,
	LATTICE_JSON_ERROR,
	LATTICE_SYNTAX_ERROR,
	LATTICE_TYPE_ERROR,
	LATTICE_VALUE_ERROR,
	LATTICE_NAME_ERROR,
	LATTICE_INCLUDE_ERROR,
} lattice_error_code;

typedef struct lattice_error {
	int line;
	lattice_error_code code;
	char *message;
} lattice_error;

typedef size_t (*lattice_emit)(const char *data, void *ctx);

#define LATTICE_IMPL(ns, ty, name, ...) size_t lattice ## ns ## name( \
	const char *template, ty root, __VA_ARGS__, lattice_opts)
#define LATTICE_IMPLS(ns, ty, ...) \
	LATTICE_IMPL(ns, ty, _file, FILE *file __VA_OPT__(,) __VA_ARGS__); \
	LATTICE_IMPL(ns, ty, _buffer, char **buffer __VA_OPT__(,) __VA_ARGS__); \
	LATTICE_IMPL(ns, ty, , lattice_emit, void *ctx __VA_OPT__(,) __VA_ARGS__)

LATTICE_IMPLS(, const void *, lattice_iface);

#ifndef LATTICE_INSIDE
#undef LATTICE_IMPL
#undef LATTICE_IMPLS
#endif // ifndef LATTICE_INSIDE

const lattice_error *lattice_get_error();

#endif // ifndef LATTICE_H
