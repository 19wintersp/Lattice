#include <stdlib.h>
#include <string.h>

#include <lattice/lattice.h>

static lattice_error error;

static void set_error(lattice_error new_error) {
	if (error.message) free(error.message);
	error = new_error;
}

const lattice_error *lattice_get_error() {
	return &error;
}

struct expr_lexeme {
	int line;
	enum {
		LEX_NULL,
		LEX_BOOLEAN,
		LEX_NUMBER,
		LEX_STRING,
		LEX_LPAREN,
		LEX_RPAREN,
		LEX_LBRACK,
		LEX_RBRACK,
		LEX_LBRACE,
		LEX_RBRACE,
		LEX_COMMA,
		LEX_DOT,
		LEX_COLON,
		LEX_EITHER,
		LEX_BOTH,
		LEX_EQ,
		LEX_NEQ,
		LEX_GT,
		LEX_GTE,
		LEX_LT,
		LEX_LTE,
		LEX_ADD,
		LEX_SUB,
		LEX_MUL,
		LEX_DIV,
		LEX_QUOT,
		LEX_MOD,
		LEX_EXP,
		LEX_AND,
		LEX_OR,
		LEX_NOT,
		LEX_ROOT,
		LEX_IDENT,
	} type;
	union {
		bool boolean;
		double number;
		char *string;
		char *ident;
	} data;
	struct expr_lexeme *next;
};

struct expr_token {
	int line;
	enum {
		EXPR_NULL,
		EXPR_BOOLEAN,
		EXPR_NUMBER,
		EXPR_STRING,
		EXPR_ARRAY,
		EXPR_OBJECT,
		EXPR_OBJECT_ITEM,
		EXPR_EITHER,
		EXPR_BOTH,
		EXPR_EQ,
		EXPR_NEQ,
		EXPR_GT,
		EXPR_GTE,
		EXPR_LT,
		EXPR_LTE,
		EXPR_ADD,
		EXPR_SUB,
		EXPR_MUL,
		EXPR_DIV,
		EXPR_QUOT,
		EXPR_MOD,
		EXPR_EXP,
		EXPR_AND,
		EXPR_OR,
		EXPR_XOR,
		EXPR_NOT,
		EXPR_NEG,
		EXPR_POS,
		EXPR_ROOT,
		EXPR_IDENT,
		EXPR_LOOKUP,
		EXPR_METHOD,
		EXPR_INDEX,
	} type;
	union {
		bool boolean;
		double number;
		char *string;
		char *ident;
		struct expr_token *expr;
	} item;
	union {
		char *ident;
		struct expr_token *expr;
	} item2;
	struct expr_token *child, *next;
};

struct token {
	int line;
	enum {
		TOKEN_SPAN,
		TOKEN_SUB_ESC,
		TOKEN_SUB_RAW,
		TOKEN_INCLUDE,
		TOKEN_IF,
		TOKEN_ELIF,
		TOKEN_ELSE,
		TOKEN_SWITCH,
		TOKEN_CASE,
		TOKEN_DEFAULT,
		TOKEN_FOR_RANGE_EXC,
		TOKEN_FOR_RANGE_INC,
		TOKEN_FOR_ITER,
		TOKEN_WITH,
		TOKEN_END,
	} type;
	char *ident;
	struct expr_token *expr, *expr2;
	struct token *prev, *next, *parent, *child;
};

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
