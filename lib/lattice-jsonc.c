#include <stdlib.h>
#include <string.h>

#include <json-c/json_tokener.h>

#include <lattice/lattice-jsonc.h>

static struct json_object *lparse(const char *src, size_t length) {
	if (length == 0) {
		return json_tokener_parse(src);
	} else {
		struct json_tokener *tokener = json_tokener_new();
		struct json_object *obj = json_tokener_parse_ex(tokener, src, length);
		json_tokener_free(tokener);
		return obj;
	}
}

static char *lprint(struct json_object *obj) {
	size_t length;
	const char *src = json_object_to_json_string_length(obj, 0, &length);

	char *clone = malloc(length + 1);
	clone[length] = 0;
	strcpy(clone, src);

	return clone;
}

static void lfree(struct json_object *obj) {
	json_object_put(obj);
}

static struct json_object *lcreate(lattice_type type, lattice_value value) {
	switch (type) {
		case LATTICE_TYPE_NULL: return json_object_new_null();
		case LATTICE_TYPE_BOOLEAN: return json_object_new_boolean(value.boolean);
		case LATTICE_TYPE_NUMBER: return json_object_new_double(value.number);
		case LATTICE_TYPE_STRING: return json_object_new_string(value.string);
		case LATTICE_TYPE_ARRAY: return json_object_new_array();
		case LATTICE_TYPE_OBJECT: return json_object_new_object();
		default: return NULL;
	}
}

static struct json_object *lclone(const struct json_object *obj) {
	struct json_object *clone = NULL;
	return json_object_deep_copy(obj, &clone, NULL) == 0 ? clone : NULL;
}

static lattice_type ltype(const struct json_object *obj) {
	static lattice_type trans[] = {
		[json_type_null] = LATTICE_TYPE_NULL,
		[json_type_boolean] = LATTICE_TYPE_BOOLEAN,
		[json_type_double] = LATTICE_TYPE_NUMBER,
		[json_type_int] = LATTICE_TYPE_NUMBER,
		[json_type_string] = LATTICE_TYPE_STRING,
		[json_type_array] = LATTICE_TYPE_ARRAY,
		[json_type_object] = LATTICE_TYPE_OBJECT,
	};

	return trans[json_object_get_type(obj)];
}

static lattice_value lvalue(const struct json_object *obj) {
	lattice_value value = { 0 };

	switch (json_object_get_type(obj)) {
		case json_type_boolean:
			value.boolean = json_object_get_boolean(obj);
			break;

		case json_type_double:
		case json_type_int:
			value.number = json_object_get_double(obj);
			break;

		case json_type_string:
			value.string = json_object_get_string((struct json_object *) obj);
			break;

		default:
			break;
	}

	return value;
}

static size_t llength(const struct json_object *obj) {
	switch (json_object_get_type(obj)) {
		case json_type_string:
			return strlen(json_object_get_string((struct json_object *) obj));

		case json_type_array:
			return json_object_array_length(obj);

		case json_type_object:
			return json_object_object_length(obj);

		default:
			return 0;
	}
}

static struct json_object *lget(
	const struct json_object *obj,
	lattice_index index
) {
	switch (json_object_get_type(obj)) {
		case json_type_array: return json_object_array_get_idx(obj, index.array);
		case json_type_object: return json_object_object_get(obj, index.object);
		default: return NULL;
	}
}

static void ladd(
	struct json_object *obj,
	const char *key,
	struct json_object *value
) {
	switch (json_object_get_type(obj)) {
		case json_type_array:
			json_object_array_add(obj, value);
			break;

		case json_type_object:
			json_object_object_add(obj, key, value);
			break;

		default:
			break;
	}
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
};

size_t lattice_jsonc(
	const char *template,
	const struct json_object *root,
	size_t (*emit)(const char *data, void *ctx), void *ctx,
	lattice_opts opts
) {
	return lattice(template, root, emit, ctx, iface, opts);
}

size_t lattice_jsonc_file(
	const char *template,
	const struct json_object *root,
	FILE *file,
	lattice_opts opts
) {
	return lattice_file(template, root, file, iface, opts);
}

size_t lattice_jsonc_buffer(
	const char *template,
	const struct json_object *root,
	char **buffer,
	lattice_opts opts
) {
	return lattice_buffer(template, root, buffer, iface, opts);
}
