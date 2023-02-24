#include <stdlib.h>
#include <string.h>

#include <lattice/lattice.h>

static size_t file_emit(const char *data, void *file) {
	return fputs(data, (FILE *) file) == EOF ? 0 : 1;
}

size_t lattice_file(
	const char *template,
	const void *root,
	FILE *file,
	lattice_iface iface,
	lattice_opts opts
) {
	return lattice(template, root, file_emit, file, iface, opts);
}

struct buffer_ctx {
	size_t length, allocated;
	char **buffer;
};

static size_t buffer_emit(const char *data, void *pctx) {
	struct buffer_ctx *ctx = pctx;

	size_t offset = ctx->length;
	ctx->length += strlen(data);

	if (ctx->length >= ctx->allocated) {
		size_t np2 = ctx->length;
		for (size_t i = 1; (i >> 3) < sizeof(size_t); i <<= 1) np2 |= np2 >> i;

		ctx->allocated = np2 + 1;
		*ctx->buffer = realloc(*ctx->buffer, ctx->allocated);

		if (*ctx->buffer == NULL) return 0;
	}

	strcpy(*ctx->buffer + offset, data);
	return 1;
}

size_t lattice_buffer(
	const char *template,
	const void *root,
	char **buffer,
	lattice_iface iface,
	lattice_opts opts
) {
	*buffer = calloc(1, 1);
	struct buffer_ctx ctx = { .length = 0, .allocated = 1, .buffer = buffer };

	return lattice(template, root, buffer_emit, &ctx, iface, opts);
}
