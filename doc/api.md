# Lattice C API

## Main functions

Lattice has one main function, and two other functions which call the main
function with predefined arguments. The functions are identical aside from their
output parameters, which substitute the emission function and context for a
different type.

### `lattice`

```c
size_t lattice(
	const char *template,
	const void *root,
	lattice_emit emit,
	void *ctx,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **err
);
```

`lattice` is the main function: it is the main entrypoint into the template
processor. It rarely needs to be invoked directly, instead being called via the
other functions.

#### Parameters

- **`template`**: the template, as a null-terminated ASCII string.
- **`root`**: the root data object to format the template with.
- **`emit`**: the output callback - see [`lattice_emit`](#lattice_emit).
- **`ctx`**: the context to pass to the callback along with the data.
- **`iface`**: the interface - see [`lattice_iface`](#lattice_iface).
- **`opts`**: running options - see [`lattice_opts`](#lattice_opts).
- **`err`**: a pointer to a variable to be set on error if non-null.

#### Return value

When no error occurs, all functions return the number of bytes written out.

### `lattice_buffer`

```c
size_t lattice_buffer(
	const char *template,
	const void *root,
	char **buffer,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **err
);
```

`lattice_buffer` outputs the data to a string buffer by using its own emission
function.

#### Parameters

- **`buffer`**: a pointer to a variable to be set to a `free`able
	null-terminated string containing the output.

### `lattice_file`

```c
size_t lattice_file(
	const char *template,
	const void *root,
	FILE *file,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **err
);
```

`lattice_file` outputs the data to a file by using its own emission function.

#### Parameters

- **`file`**: a standard C file pointer to write output to.

## Interface functions

The primary functions take an arbitrary `void *` and a `lattice_iface` in order
to generalise the functions over any JSON type. Considering this is impractical
for most uses, functions are provided for three common C JSON libraries, which
replace the `void *` with the JSON type and pass their own implementation of
`lattice_iface`. The functions place the name of the library (one of `cjson`,
`jsonc`, or `jansson`) after `lattice` (for example `lattice_cjson_file`) and
have the same signature and functionality, except for the aforementioned
modifications.

## Structures and types

### `lattice_opts`

```c
typedef struct lattice_opts {
	const char * const *search;
	char *(*resolve)(const char *);
	char *(*escape)(const char *);
	bool ignore_emit_zero;
	...
} lattice_opts;
```

`lattice_opts` specifies the options that affect the way the template is
processed. All of the fields are optional, and a zero-filled struct represents
a valid default configuration.

The `resolve` and `search` fields combine specially to determine the behaviour
of the include resolution system:

|                         |       `search` is `NULL`      |          `search` is non-`NULL`         |
|------------------------:|:-----------------------------:|:---------------------------------------:|
|     `resolve` is `NULL` |   Search CWD to obtain path   | Search paths in `search` to obtain path |
| `resolve` is non-`NULL` | Call `resolve` to obtain path |    Call `resolve` to obtain contents    |

#### Fields

- **`search`**: null, or a pointer to a null-terminated array of directories to
	search to find a requested path.
- **`resolve`**: null, or a function returning a `free`able string equating to
	either the resolved path or contents of the include passed as the parameter.
- **`escape`**: null, or a function returning a `free`able string which is the
	escaped version of the data passed as the parameter; by default, uses HTML
	escaping (replaces `'`, `"`, `<`, `>`, and `&`).
- **`ignore_emit_zero`**: if the `emit` function returns zero, by default it is
	interpreted as an error; if this field is set to `true`, it will be ignored.

### `lattice_error`

```c
typedef struct lattice_error {
	int line; char *file;
	lattice_error_code code;
	char *message;
} lattice_error;
```

`lattice_error` describes an error that has occurred. `lattice_error`s returned
must not be mutated, and should be freed with `lattice_error_free`.

#### Fields

- **`line`**: the line on which the error occurred, or a nonsense value below 1.
- **`file`**: null, or the included file in which the error occurred.
- **`code`**: the type of error - see [the error index](#errors).
- **`message`**: a null-terminated ASCII string describing the error. These are
	subject to change without notice.

### `lattice_emit`

```c
typedef size_t (*lattice_emit)(const char *data, void *ctx);
```

`lattice_emit` is a convenience alias for the function type used for arbitrary
output.

#### Parameters

- **`data`**: a constant null-terminated ASCII string containing the data to be
	written.
- **`ctx`**: a context passed into the `lattice` function.

#### Return value

An emission function should return the number of bytes written out. In case of
error, it should return zero (the emission function should not be called with an
empty string).

### `lattice_iface`

```c
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
	void (*keys)(const void *, const char *[]);
} lattice_iface;
```

`lattice_iface` (and associated types) is used to define custom JSON types. Most
users do not need to use it.

#### Fields

- **`parse`**: returns an allocated JSON object, parsed from the JSON string in
	the first argument; if the second argument is zero, it is null-terminated,
	otherwise that long.
- **`print`**: returns a `free`able null-terminated string, representing the
	JSON string for the passed object.
- **`free`**: recursively frees the passed JSON object.
- **`create`**: creates a new JSON object with the given type and value (for
	primitives; containers are initialised to be empty).
- **`clone`**: allocates a deep copy of the passed JSON object.
- **`type`**: returns the type of the passed JSON object.
- **`value`**: if the passed JSON object is primitive, returns its value.
- **`length`**: if the passed JSON object is a string, array, or object, returns
	its length.
- **`get`**: returns the indexed item from the container. The return value will
	not be freed.
- **`add`**: adds an item to the container (for arrays, the key will be null).
- **`keys`**: initialises the string array passed as the second argument to the
	keys of the object passed as the first argument.

## Errors

Errors are defined in the enum `lattice_error_code`.

### IO error (`LATTICE_IO_ERROR`)

Returned when an emission function returns zero, or an IO error occurs (such as
when a filesystem call fails whilst resolving an include).

### Options error (`LATTICE_OPTS_ERROR`)

Returned when options passed to `lattice` are invalid.

### JSON error (`LATTICE_JSON_ERROR`)

Returned when an issue occurs with the JSON interface.

### Syntax error (`LATTICE_SYNTAX_ERROR`)

Returned when there is invalid syntax in the template, regardless of the context
in which it takes place (macro or expression).

### Type error (`LATTICE_TYPE_ERROR`)

Returned when there is an incorrect JSON type for the expected value.

### Value error (`LATTICE_VALUE_ERROR`)

Returned when there is an invalid JSON value for the expected value.

### Name error (`LATTICE_NAME_ERROR`)

Returned when there is reference to a nonexistent key.

### Include error (`LATTICE_INCLUDE_ERROR`)

Returned when there is an error whilst trying to resolve or read an include,
including if a reoslution function returns null.

## Versioning constants

Four macro constants are defined:

- **`LATTICE_VERSION_MAJOR`**: the major version.
- **`LATTICE_VERSION_MINOR`**: the minor version.
- **`LATTICE_VERSION_PATCH`**: the patch version.
- **`LATTICE_VERSION_STR`**: the full version, represented as a string.
