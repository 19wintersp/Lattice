#if !(defined(TOOL_CJSON) || defined(TOOL_JSONC) || defined(TOOL_JANSSON))
#error no valid tool library selected
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TOOL_CJSON)
#include <lattice/lattice-cjson.h>
#elif defined(TOOL_JSONC)
#include <json-c/json_tokener.h>
#include <lattice/lattice-jsonc.h>
#elif defined(TOOL_JANSSON)
#include <lattice/lattice-jansson.h>
#endif

void usage();
void help();
char *read_file(FILE *);

static const char *error_names[] = {
	[LATTICE_UNKNOWN_ERROR] = "Unknown",
	[LATTICE_ALLOC_ERROR]   = "Memory",
	[LATTICE_IO_ERROR]      = "IO",
	[LATTICE_OPTS_ERROR]    = "Option",
	[LATTICE_JSON_ERROR]    = "JSON",
	[LATTICE_SYNTAX_ERROR]  = "Syntax",
	[LATTICE_TYPE_ERROR]    = "Type",
	[LATTICE_VALUE_ERROR]   = "Value",
	[LATTICE_NAME_ERROR]    = "Name",
	[LATTICE_INCLUDE_ERROR] = "Include",
};

int main(int argc, char *argv[]) {
	if (argc < 2) usage();
	if (strcmp(argv[1], "--help") == 0) help();

	char *json_buf = read_file(stdin);
	if (!json_buf) {
		fputs("Error: failed to read stdin\n", stderr);
		return 2;
	}

#if defined(TOOL_CJSON)
	cJSON *json = cJSON_Parse(json_buf);
#elif defined(TOOL_JSONC)
	struct json_object *json = json_tokener_parse(json_buf);
#elif defined(TOOL_JANSSON)
	json_t *json = json_loads(json_buf, JSON_REJECT_DUPLICATES, NULL);
#endif

	free(json_buf);
	if (!json) {
		fputs("Error: failed to parse JSON\n", stderr);
		return 3;
	}

	int exit = 0;
	lattice_error *err = NULL;

	for (int i = 1; i < argc; i++) {
		FILE *file = fopen(argv[i], "r");
		if (!file) {
			fprintf(stderr, "Error: failed to open '%s'\n", argv[i]);
			exit = 2;
			goto end;
		}

		char *src = read_file(file);
		fclose(file);

		if (!src) {
			fprintf(stderr, "Error: failed to read '%s'\n", argv[i]);
			exit = 2;
			goto end;
		}

#if defined(TOOL_CJSON)
		lattice_cjson_file(src, json, stdout, (lattice_opts) {}, &err);
#elif defined(TOOL_JSONC)
		lattice_jsonc_file(src, json, stdout, (lattice_opts) {}, &err);
#elif defined(TOOL_JANSSON)
		lattice_jansson_file(src, json, stdout, (lattice_opts) {}, &err);
#endif

		free(src);

		if (err) {
			fprintf(
				stderr, "%s error: %s (%s:%d)\n", error_names[err->code],
				err->message, err->file ? err->file : argv[i], err->line
			);

			exit = 4;
			goto end;
		}
	}

end:
#if defined(TOOL_CJSON)
	cJSON_Delete(json);
#elif defined(TOOL_JSONC)
	json_object_put(json);
#elif defined(TOOL_JANSSON)
	json_decref(json);
#endif

	return exit;
}

void usage() {
	fputs("Usage: lattice TEMPLATES...\n", stderr);
	fputs("Try 'lattice --help' for more information.\n", stderr);

	exit(1);
}

void help() {
	puts("Usage: lattice TEMPLATES...");
	puts("Format TEMPLATES using JSON parsed from stdin.");
	puts("Multiple templates are concatenated.");
	puts("");
	puts("No options are available, other than this help page.");
	puts("");
	puts("Exit status:");
	puts("  0    completed successfully");
	puts("  1    argument error");
	puts("  2    IO error");
	puts("  3    JSON parsing error");
	puts("  4    templating error");
	puts("");
	puts("MIT licence");
	puts("This is free software: you may change and share it.");
	puts("There is no warranty, to the extent permitted by law.");
	puts("");
	puts("Lattice version " LATTICE_VERSION_STR);

	exit(0);
}

char *read_file(FILE *file) {
	char *buf = malloc(256), *buf_sub = buf;
	size_t buf_size = 256;

	while (!feof(file)) {
		if (fgets(buf_sub, 256, file) == NULL && ferror(file)) {
			free(buf);
			return NULL;
		}

		size_t offset = strlen(buf_sub);
		char *new_buf = realloc(buf, buf_size + 256);
		buf_size += 256;
		buf_sub = new_buf + (buf_sub - buf) + offset;
		buf = new_buf;
	}

	return buf;
}
