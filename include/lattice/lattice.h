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

#define LATTICE_IMPL(name, arg) size_t lattice ## name( \
	const char *template, const void *root, lattice_iface, lattice_opts, arg)
LATTICE_IMPL(,        size_t (*emit)(const char *));
LATTICE_IMPL(_file,   FILE *file);
LATTICE_IMPL(_buffer, char **buffer);
#undef LATTICE_IMPL

const lattice_error *lattice_get_error();

#endif // ifndef LATTICE_H
