#include <string.h>

#include <lattice/lattice-jansson.h>

static json_t *lparse(const char *src, size_t length) {
	if (length == 0) return json_loads(src, JSON_REJECT_DUPLICATES, NULL);
	else return json_loadb(src, length, JSON_REJECT_DUPLICATES, NULL);
}

static char *lprint(const json_t *obj) {
	return json_dumps(obj, 0);
}

static void lfree(json_t *obj) {
	json_decref(obj);
}

static json_t *lcreate(lattice_type type, lattice_value value) {
	switch (type) {
		case LATTICE_TYPE_NULL:    return json_null();
		case LATTICE_TYPE_BOOLEAN: return json_boolean(value.boolean);
		case LATTICE_TYPE_NUMBER:  return json_real(value.number);
		case LATTICE_TYPE_STRING:  return json_string(value.string);
		case LATTICE_TYPE_ARRAY:   return json_array();
		case LATTICE_TYPE_OBJECT:  return json_object();
		default:                   return NULL;
	}
}

static json_t *lclone(const json_t *obj) {
	return obj ? json_deep_copy(obj) : NULL;
}

static lattice_type ltype(const json_t *obj) {
	static lattice_type trans[] = {
		[JSON_NULL]    = LATTICE_TYPE_NULL,
		[JSON_TRUE]    = LATTICE_TYPE_BOOLEAN,
		[JSON_FALSE]   = LATTICE_TYPE_BOOLEAN,
		[JSON_REAL]    = LATTICE_TYPE_NUMBER,
		[JSON_INTEGER] = LATTICE_TYPE_NUMBER,
		[JSON_STRING]  = LATTICE_TYPE_STRING,
		[JSON_ARRAY]   = LATTICE_TYPE_ARRAY,
		[JSON_OBJECT]  = LATTICE_TYPE_OBJECT,
	};

	return trans[json_typeof(obj)];
}

static lattice_value lvalue(const json_t *obj) {
	lattice_value value = { 0 };

	switch (json_typeof(obj)) {
		case JSON_TRUE:
			value.boolean = true;
			break;

		case JSON_FALSE:
			value.boolean = false;
			break;

		case JSON_REAL:
		case JSON_INTEGER:
			value.number = json_number_value(obj);
			break;

		case JSON_STRING:
			value.string = json_string_value(obj);
			break;

		default:
			break;
	}

	return value;
}

static size_t llength(const json_t *obj) {
	switch (json_typeof(obj)) {
		case JSON_STRING: return strlen(json_string_value(obj));
		case JSON_ARRAY:  return json_array_size(obj);
		case JSON_OBJECT: return json_object_size(obj);
		default:          return 0;
	}
}

static json_t *lget(const json_t *obj, lattice_index index) {
	switch (json_typeof(obj)) {
		case JSON_ARRAY:  return json_array_get(obj, index.array);
		case JSON_OBJECT: return json_object_get(obj, index.object);
		default:          return NULL;
	}
}

static void ladd(json_t *obj, const char *key, json_t *value) {
	switch (json_typeof(obj)) {
		case JSON_ARRAY:
			json_array_append_new(obj, value);
			break;

		case JSON_OBJECT:
			json_object_set_new(obj, key, value);
			break;

		default:
			break;
	}
}

static void lkeys(const json_t *obj, const char *out[]) {
	if (json_typeof(obj) != JSON_OBJECT) return;

	size_t i = 0;

	for (
		const char *key = json_object_iter_key(json_object_iter(obj));
		key; key = json_object_iter_key(
			json_object_iter_next(obj, json_object_key_to_iter(key)))
	) out[i++] = key;
}

static lattice_iface iface = {
	.parse  = (void *(*)(const char *, size_t)) lparse,
	.print  = (char *(*)(const void *)) lprint,
	.free   = (void (*)(void *)) lfree,
	.create = (void *(*)(lattice_type, lattice_value)) lcreate,
	.clone  = (void *(*)(const void *)) lclone,
	.type   = (lattice_type (*)(const void *)) ltype,
	.value  = (lattice_value (*)(const void *)) lvalue,
	.length = (size_t (*)(const void *)) llength,
	.get    = (void *(*)(const void *, lattice_index)) lget,
	.add    = (void (*)(void *, const char *, void *)) ladd,
	.keys   = (void (*)(const void *, const char *[])) lkeys,
};

size_t lattice_jansson(
	const char *template,
	const json_t *root,
	size_t (*emit)(const char *data, void *ctx), void *ctx,
	lattice_opts opts
) {
	return lattice(template, root, emit, ctx, iface, opts);
}

size_t lattice_jansson_file(
	const char *template,
	const json_t *root,
	FILE *file,
	lattice_opts opts
) {
	return lattice_file(template, root, file, iface, opts);
}

size_t lattice_jansson_buffer(
	const char *template,
	const json_t *root,
	char **buffer,
	lattice_opts opts
) {
	return lattice_buffer(template, root, buffer, iface, opts);
}
