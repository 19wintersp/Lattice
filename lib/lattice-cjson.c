#include <string.h>

#include <lattice/lattice-cjson.h>

static cJSON *lparse(const char *src, size_t length) {
	if (length == 0) return cJSON_Parse(src);
	else return cJSON_ParseWithLength(src, length);
}

static char *lprint(const cJSON *obj) {
	return cJSON_Print(obj);
}

static void lfree(cJSON *obj) {
	cJSON_Delete(obj);
}

static cJSON *lcreate(lattice_type type, lattice_value value) {
	switch (type) {
		case LATTICE_TYPE_NULL:
			return cJSON_CreateNull();

		case LATTICE_TYPE_BOOLEAN:
			return cJSON_CreateBool((cJSON_bool) value.boolean);

		case LATTICE_TYPE_NUMBER:
			return cJSON_CreateNumber(value.number);

		case LATTICE_TYPE_STRING:
			return cJSON_CreateString(value.string);

		case LATTICE_TYPE_ARRAY:
			return cJSON_CreateArray();

		case LATTICE_TYPE_OBJECT:
			return cJSON_CreateObject();

		default:
			return NULL;
	}
}

static lattice_type ltype(const cJSON *obj) {
	if (cJSON_IsBool(obj)) return LATTICE_TYPE_BOOLEAN;
	if (cJSON_IsNumber(obj)) return LATTICE_TYPE_NUMBER;
	if (cJSON_IsString(obj)) return LATTICE_TYPE_STRING;
	if (cJSON_IsArray(obj)) return LATTICE_TYPE_ARRAY;
	if (cJSON_IsObject(obj)) return LATTICE_TYPE_OBJECT;

	return LATTICE_TYPE_NULL;
}

static lattice_value lvalue(const cJSON *obj) {
	lattice_value value = { 0 };

	if (cJSON_IsBool(obj)) value.boolean = cJSON_IsTrue(obj);
	if (cJSON_IsNumber(obj)) value.number = cJSON_GetNumberValue(obj);
	if (cJSON_IsString(obj)) value.string = cJSON_GetStringValue(obj);

	return value;
}

static size_t llength(const cJSON *obj) {
	if (cJSON_IsString(obj)) return strlen(cJSON_GetStringValue(obj));
	if (cJSON_IsArray(obj) || cJSON_IsObject(obj)) return cJSON_GetArraySize(obj);

	return 0;
}

static cJSON *lget(const cJSON *obj, lattice_index index) {
	if (cJSON_IsArray(obj)) return cJSON_GetArrayItem(obj, index.array);
	if (cJSON_IsObject(obj)) return cJSON_GetObjectItem(obj, index.object);

	return NULL;
}

static void ladd(cJSON *obj, const char *key, cJSON *value) {
	if (cJSON_IsArray(obj)) cJSON_AddItemToArray(obj, value);
	else if (cJSON_IsObject(obj)) cJSON_AddItemToObject(obj, key, value);
}

static lattice_iface iface = {
	.parse  = (void *(*)(const char *, size_t)) lparse,
	.print  = (char *(*)(const void *)) lprint,
	.free   = (void (*)(void *)) lfree,
	.create = (void *(*)(lattice_type, lattice_value)) lcreate,
	.type   = (lattice_type (*)(const void *)) ltype,
	.value  = (lattice_value (*)(const void *)) lvalue,
	.length = (size_t (*)(const void *)) llength,
	.get    = (void *(*)(const void *, lattice_index)) lget,
	.add    = (void (*)(void *, const char *, void *)) ladd,
};

size_t lattice_cjson(
	const char *template,
	const cJSON *root,
	lattice_opts opts,
	size_t (*emit)(const char *)
) {
	return lattice(template, root, iface, opts, emit);
}

size_t lattice_cjson_file(
	const char *template,
	const cJSON *root,
	lattice_opts opts,
	FILE *file
) {
	return lattice_file(template, root, iface, opts, file);
}

size_t lattice_cjson_buffer(
	const char *template,
	const cJSON *root,
	lattice_opts opts,
	char **buffer
) {
	return lattice_buffer(template, root, iface, opts, buffer);
}
