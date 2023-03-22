#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lattice/lattice.h>

static char *astrdup(const char *src) {
	char *clone = malloc(strlen(src) + 1);
	strcpy(clone, src); return clone;
}

static char *astrndup(const char *src, size_t n) {
	char *clone = malloc(n + 1); clone[n] = 0;
	strncpy(clone, src, n); return clone;
}

static char *format(const char *fmt, ...) {
	va_list list1;
	va_start(list1, fmt);

	va_list list2;
	va_copy(list2, list1);

	int length = vsnprintf(NULL, 0, fmt, list1);
	va_end(list1);

	char* buffer = malloc(length + 1);
	vsnprintf(buffer, length + 1, fmt, list2);
	va_end(list2);

	return buffer;
}

static lattice_error error;

static void set_error(lattice_error new_error) {
	free(error.message);
	error = new_error;
}

const lattice_error *lattice_get_error() {
	return &error;
}

struct expr_lexeme {
	int line;
	enum expr_lexeme_type {
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
		LEX_NOT,
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
		LEX_XOR,
		LEX_COMP,
		LEX_ROOT,
		LEX_IDENT,
		LEX_OPT,
	} type;
	union {
		bool boolean;
		double number;
		char *string;
		char *ident;
	} data;
	struct expr_lexeme *next;
};

static void free_expr_lexeme(struct expr_lexeme *lex) {
	if (lex == NULL) return;
	free_expr_lexeme(lex->next);

	if (lex->type == LEX_STRING) free(lex->data.string);
	if (lex->type == LEX_IDENT) free(lex->data.ident);

	free(lex);
}

struct expr_token {
	int line;
	enum expr_token_type {
		EXPR_NULL,
		EXPR_BOOLEAN,
		EXPR_NUMBER,
		EXPR_STRING,
		EXPR_ARRAY,
		EXPR_OBJECT,
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
		EXPR_COMP,
		EXPR_NOT,
		EXPR_NEG,
		EXPR_POS,
		EXPR_ROOT,
		EXPR_IDENT,
		EXPR_LOOKUP,
		EXPR_METHOD,
		EXPR_INDEX,
		EXPR_TERNARY,
	} type;
	union {
		bool boolean;
		double number;
		char *string;
		char *ident;
		struct expr_token *expr;
	} item[3];
	struct expr_token *next;
};

static void free_expr_token(struct expr_token *expr) {
	if (!expr) return;
	free_expr_token(expr->next);

	if (expr->type == EXPR_STRING) free(expr->item[0].string);
	else if (expr->type == EXPR_IDENT) free(expr->item[0].ident);
	else if (
		expr->type != EXPR_BOOLEAN &&
		expr->type != EXPR_NUMBER
	) free_expr_token(expr->item[0].expr);

	if (expr->type == EXPR_LOOKUP || expr->type == EXPR_METHOD)
		free(expr->item[1].ident);
	else free_expr_token(expr->item[1].expr);

	free_expr_token(expr->item[2].expr);

	free(expr);
}

struct token {
	int line;
	enum token_type {
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
	struct expr_token *expr[2];
	struct token *prev, *next, *parent, *child;
};

static void free_token(struct token *token) {
	if (token->child) free_token(token->child);
	if (token->next) free_token(token->next);

	free(token->ident);
	if (token->expr[0]) free_expr_token(token->expr[0]);
	if (token->expr[1]) free_expr_token(token->expr[1]);

	free(token);
}

#define LEX_ADD(ty) \
	lex = calloc(1, sizeof(struct expr_lexeme)); \
	*lexp = lex; lexp = &lex->next; \
	lex->line = *line; lex->type = ty;
#define LEX_SUBCASE(ch, ty) if (expr[s] == ch) { LEX_ADD(ty); s++; break; }
#define LEX_CP(ch, ty) LEX_SUBCASE(ch, ty)
#define LEX_CPW(...) __VA_OPT__(LEX_CP(__VA_ARGS__))
#define LEX_CASE(ch, ty, ...) case ch: LEX_CPW(__VA_ARGS__) LEX_ADD(ty); break;

static size_t lex_expr(
	const char *expr,
	const char *term,
	int *line,
	struct expr_lexeme **lexp
) {
	size_t s = 0;
	size_t tlen = term ? strlen(term) : 0;

	struct expr_lexeme *lex = NULL;
	int brackets = 0;

	while (
		expr[s] &&
		(!term || brackets > 0 || strncmp(expr + s, term, tlen) != 0)
	) {
		char c = expr[s++];
		switch (c) {
			case '\n':
				(*line)++;
				break;

			LEX_CASE('(', LEX_LPAREN);
			LEX_CASE(')', LEX_RPAREN);
			LEX_CASE('[', LEX_LBRACK);
			LEX_CASE(']', LEX_RBRACK);
			LEX_CASE('{', LEX_LBRACE);
			LEX_CASE('}', LEX_RBRACE);
			LEX_CASE(',', LEX_COMMA);
			LEX_CASE('.', LEX_DOT);
			LEX_CASE(':', LEX_COLON);
			LEX_CASE('|', LEX_OR, '|', LEX_EITHER);
			LEX_CASE('&', LEX_AND, '&', LEX_BOTH);
			LEX_CASE('^', LEX_XOR);
			LEX_CASE('~', LEX_COMP);
			LEX_CASE('=', LEX_EQ, '=', LEX_EQ);
			LEX_CASE('!', LEX_NOT, '=', LEX_NEQ);
			LEX_CASE('>', LEX_GT, '=', LEX_GTE);
			LEX_CASE('<', LEX_LT, '=', LEX_LTE);
			LEX_CASE('+', LEX_ADD);
			LEX_CASE('-', LEX_SUB);
			LEX_CASE('*', LEX_MUL, '*', LEX_EXP);
			LEX_CASE('/', LEX_DIV, '/', LEX_QUOT);
			LEX_CASE('%', LEX_MOD);
			LEX_CASE('@', LEX_ROOT);
			LEX_CASE('?', LEX_OPT);

			case '"':
			case '\'': {}
				char quote = c;

				char *contents = malloc(1);
				size_t length = 0;

				while ((c = expr[s++]) != quote) {
					size_t i = length++;
					contents = realloc(contents, length + 1);
					contents[i] = c;

					if (c == '\\') {
						switch (expr[s++]) {
							case 'a':  contents[i] = '\a'; break;
							case 'b':  contents[i] = '\b'; break;
							case 'e':  contents[i] = '\e'; break;
							case 'f':  contents[i] = '\f'; break;
							case 'n':  contents[i] = '\n'; break;
							case 'r':  contents[i] = '\r'; break;
							case 't':  contents[i] = '\t'; break;
							case 'v':  contents[i] = '\v'; break;
							case '\\': contents[i] = '\\'; break;
							case '\'': contents[i] = '\''; break;
							case '\"': contents[i] = '\"'; break;

							case 'x': {}
								char hi = expr[s++];
								char lo = expr[s++];

								if (!isxdigit(hi) || !isxdigit(lo)) {
									set_error((lattice_error) {
										.line = *line,
										.code = LATTICE_SYNTAX_ERROR,
										.message = astrdup("invalid hex literal"),
									});

									free(contents);
									return 0;
								}

								hi = hi <= '9' ? hi - '0' : 10 + tolower(hi) - 'a';
								lo = lo <= '9' ? lo - '0' : 10 + tolower(lo) - 'a';

								contents[i] = hi << 4 | lo;

								break;

							default:
								set_error((lattice_error) {
									.line = *line,
									.code = LATTICE_SYNTAX_ERROR,
									.message = format("invalid string escape '%c'", expr[s - 1]),
								});

								free(contents);
								return 0;
						}
					}
				}

				contents[length] = 0;

				LEX_ADD(LEX_STRING);
				lex->data.string = contents;

				break;

			default:
				if (isdigit(c)) {
					const char *start = expr + s - 1;
					int base = 10;
					double number = (double) (c - '0');

					if (c == '0') {
						switch (expr[s]) {
							case 'b': base = 2; break;
							case 'o': base = 8; break;
							case 'x': base = 16; break;

							default:
								if (isdigit(expr[s])) {
									set_error((lattice_error) {
										.line = *line,
										.code = LATTICE_SYNTAX_ERROR,
										.message = astrdup("decimal literal with leading zero"),
									});

									return 0;
								}
						}

						if (base != 10) s++;
					}

					int dbase = base > 10 ? 10 : base;

					while (
						(base == 16 && isxdigit(expr[s])) ||
						('0' <= expr[s] && expr[s] < '0' + dbase)
					) {
						number *= base;
						number += expr[s] <= '9'
							? expr[s] - '0'
							: 10 + tolower(expr[s]) - 'a';

						s++;
					}

					if (base == 10) {
						if (expr[s] == '.') {
							s++;

							while (isdigit(expr[s])) s++;
						}

						if (expr[s] == 'E' || expr[s] == 'e') {
							s++;

							if (expr[s] == '+' || expr[s] == '-') s++;

							size_t peds = s;
							while (isdigit(expr[s])) s++;

							if (s == peds) {
								set_error((lattice_error) {
									.line = *line,
									.code = LATTICE_SYNTAX_ERROR,
									.message = astrdup("exponent cannot be empty"),
								});

								return 0;
							}
						}

						char *lit_clone = astrndup(start, expr + s - start);
						number = atof(lit_clone);
						free(lit_clone);
					}

					if (!expr[s] || ispunct(expr[s]) || isspace(expr[s])) {
						LEX_ADD(LEX_NUMBER);
						lex->data.number = number;
					} else {
						set_error((lattice_error) {
							.line = *line,
							.code = LATTICE_SYNTAX_ERROR,
							.message = format("unexpected character '%c'", expr[s]),
						});

						return 0;
					}
				} else if (isalpha(c) || c == '_') {
					const char *start = expr + s - 1;
					while (isalnum(expr[s]) || expr[s] == '_') s++;

					char *ident = astrndup(start, expr + s - start);
					if (strcmp(ident, "null") == 0) {
						LEX_ADD(LEX_NULL);
					} else if (strcmp(ident, "true") == 0) {
						LEX_ADD(LEX_BOOLEAN);
						lex->data.boolean = true;
					} else if (strcmp(ident, "false") == 0) {
						LEX_ADD(LEX_BOOLEAN);
						lex->data.boolean = false;
					} else {
						LEX_ADD(LEX_IDENT);
						lex->data.ident = ident;
					}

					if (lex->type != LEX_IDENT) free(ident);
				} else if (!isspace(c)) {
					set_error((lattice_error) {
						.line = *line,
						.code = LATTICE_SYNTAX_ERROR,
						.message = format("unexpected character '%c'", c),
					});

					return 0;
				}

				break;
		}

		if (lex) {
			switch (lex->type) {
				case LEX_LPAREN:
				case LEX_LBRACK:
				case LEX_LBRACE:
					brackets++;
					break;

				case LEX_RPAREN:
				case LEX_RBRACK:
				case LEX_RBRACE:
					brackets--;
					break;

				default:
					break;
			}

			lex = NULL;
		}
	}

	return s;
}

#define PARSE_MATCH(ty) \
	(*lexp && (*lexp)->type == ty && (lex = *lexp, *lexp = (*lexp)->next, true))
#define PARSE_TOK(...) \
	struct expr_token s = { .line = lex->line, __VA_ARGS__ }; \
	tok = calloc(1, sizeof(struct expr_token)); *tok = s;
#define PARSE_ERR(...) set_error((lattice_error) { \
	.line = lex->line, \
	.code = LATTICE_SYNTAX_ERROR, \
	.message = format(__VA_ARGS__), \
})

static struct expr_token *parse_expr(struct expr_lexeme **lexp);

static struct expr_token *parse_primary(struct expr_lexeme **lexp) {
	struct expr_lexeme *lex;
	struct expr_token *tok;

	if (PARSE_MATCH(LEX_NULL)) {
		PARSE_TOK(.type = EXPR_NULL);
		return tok;
	}

	if (PARSE_MATCH(LEX_BOOLEAN)) {
		PARSE_TOK(.type = EXPR_BOOLEAN, .item[0].boolean = lex->data.boolean);
		return tok;
	}

	if (PARSE_MATCH(LEX_NUMBER)) {
		PARSE_TOK(.type = EXPR_NUMBER, .item[0].number = lex->data.number);
		return tok;
	}

	if (PARSE_MATCH(LEX_STRING)) {
		PARSE_TOK(.type = EXPR_STRING, .item[0].string = lex->data.string);
		lex->data.string = NULL;
		return tok;
	}

	if (PARSE_MATCH(LEX_ROOT)) {
		PARSE_TOK(.type = EXPR_ROOT);
		return tok;
	}

	if (PARSE_MATCH(LEX_IDENT)) {
		PARSE_TOK(.type = EXPR_IDENT, .item[0].ident = lex->data.ident);
		lex->data.ident = NULL;
		return tok;
	}

	if (PARSE_MATCH(LEX_LPAREN)) {
		tok = parse_expr(lexp);
		if (!tok || PARSE_MATCH(LEX_RPAREN)) return tok;

		free_expr_token(tok);
		PARSE_ERR("expected closing parenthesis after group");
		return NULL;
	}

	if (PARSE_MATCH(LEX_LBRACK)) {
		struct expr_token *value = NULL, *last, *next;
		if (!PARSE_MATCH(LEX_RBRACK)) {
			do {
				next = parse_expr(lexp);
				if (!next) return NULL;

				if (value) {
					last->next = next;
					last = next;
				} else {
					value = last = next;
				}
			} while (PARSE_MATCH(LEX_COMMA));

			if (!PARSE_MATCH(LEX_RBRACK)) {
				free_expr_token(value);
				PARSE_ERR("expected closing bracket after array values");
				return NULL;
			}
		}

		PARSE_TOK(.type = EXPR_ARRAY, .item[0].expr = value);
		return tok;
	}

	if (PARSE_MATCH(LEX_LBRACE)) {
		struct expr_token *key = NULL, *lastk, *nextk;
		struct expr_token *value = NULL, *lastv, *nextv;
		if (!PARSE_MATCH(LEX_RBRACE)) {
			do {
				nextk = parse_expr(lexp);
				if (!nextk) return NULL;

				if (key) {
					lastk->next = nextk;
					lastk = nextk;
				} else {
					key = lastk = nextk;
				}

				if (!PARSE_MATCH(LEX_COLON)) {
					free_expr_token(key);
					free_expr_token(value);
					PARSE_ERR("expected colon after object key");
					return NULL;
				}

				nextv = parse_expr(lexp);
				if (!nextv) return NULL;

				if (value) {
					lastv->next = nextv;
					lastv = nextv;
				} else {
					value = lastv = nextv;
				}
			} while (PARSE_MATCH(LEX_COMMA));

			if (!PARSE_MATCH(LEX_RBRACE)) {
				free_expr_token(key);
				free_expr_token(value);
				PARSE_ERR("expected closing brace after object entries");
				return NULL;
			}
		}

		PARSE_TOK(.type = EXPR_OBJECT, .item[0].expr = key, .item[1].expr = value);
		return tok;
	}

	if ((lex = *lexp)) PARSE_ERR("expected expression");

	return NULL;
}

static struct expr_token *parse_call(struct expr_lexeme **lexp) {
	struct expr_lexeme *lex;
	struct expr_token *tok = parse_primary(lexp);
	if (!tok) return NULL;

	for (;;) {
		if (PARSE_MATCH(LEX_DOT)) {
			if (PARSE_MATCH(LEX_IDENT)) {
				PARSE_TOK(
					.type = EXPR_LOOKUP,
					.item[0].expr = tok,
					.item[1].ident = lex->data.ident
				);
				lex->data.ident = NULL;

				if (PARSE_MATCH(LEX_LPAREN)) {
					struct expr_token *arg = NULL, *last, *next;
					if (!PARSE_MATCH(LEX_RPAREN)) {
						do {
							next = parse_expr(lexp);
							if (!next) {
								free_expr_token(tok);
								return NULL;
							}

							if (arg) {
								last->next = next;
								last = next;
							} else {
								arg = last = next;
							}
						} while (PARSE_MATCH(LEX_COMMA));

						if (!PARSE_MATCH(LEX_RPAREN)) {
							free_expr_token(tok);
							free_expr_token(arg);
							PARSE_ERR("expected closing parenthesis after arguments");
							return NULL;
						}
					}

					tok->type = EXPR_METHOD;
					tok->item[2].expr = arg;
				}

				break;
			} else {
				free_expr_token(tok);
				PARSE_ERR("expected identifier after dot");
				return NULL;
			}
		} else if (PARSE_MATCH(LEX_LBRACK)) {
			struct expr_token *index = parse_expr(lexp);
			if (!index) {
				free_expr_token(tok);
				return NULL;
			}

			if (!PARSE_MATCH(LEX_RBRACK)) {
				free_expr_token(tok);
				free_expr_token(index);
				PARSE_ERR("expected closing bracket after subscription");
				return NULL;
			}

			PARSE_TOK(.type = EXPR_INDEX, .item[0].expr = tok, .item[1].expr = index);
		} else break;
	}

	return tok;
}

static struct {
	enum expr_lexeme_type lex;
	enum expr_token_type expr;
} unary_op[] = {
	{ LEX_ADD,  EXPR_POS },
	{ LEX_SUB,  EXPR_NEG },
	{ LEX_NOT,  EXPR_NOT },
	{ LEX_COMP, EXPR_COMP },
};

static struct expr_token *parse_unary(struct expr_lexeme **lexp) {
	struct expr_lexeme *lex;
	struct expr_token *tok;

	for (size_t i = 0; i < sizeof(unary_op) / sizeof(unary_op[0]); i++) {
		if (PARSE_MATCH(unary_op[i].lex)) {
			tok = parse_unary(lexp);
			if (!tok) return NULL;

			PARSE_TOK(.type = unary_op[i].expr, .item[0].expr = tok);
			return tok;
		}
	}

	return parse_call(lexp);
}

static int binary_op_prec[] = { 0, 2, 8, 11, 13, 18 };
static struct {
	enum expr_lexeme_type lex;
	enum expr_token_type expr;
} binary_op[] = {
	{ LEX_BOTH, EXPR_BOTH },
	{ LEX_EITHER, EXPR_EITHER },
	{ LEX_EQ,   EXPR_EQ },
	{ LEX_NEQ,  EXPR_NEQ },
	{ LEX_LT,   EXPR_LT },
	{ LEX_LTE,  EXPR_LTE },
	{ LEX_GT,   EXPR_GT },
	{ LEX_GTE,  EXPR_GTE },
	{ LEX_AND,  EXPR_AND },
	{ LEX_OR,   EXPR_OR },
	{ LEX_XOR,  EXPR_XOR },
	{ LEX_ADD,  EXPR_ADD },
	{ LEX_SUB,  EXPR_SUB },
	{ LEX_MUL,  EXPR_MUL },
	{ LEX_DIV,  EXPR_DIV },
	{ LEX_QUOT, EXPR_QUOT },
	{ LEX_EXP,  EXPR_EXP },
	{ LEX_MOD,  EXPR_MOD },
};

static struct expr_token *parse_binary(struct expr_lexeme **lexp, size_t prec) {
	if (prec >= sizeof(binary_op_prec) / sizeof(binary_op_prec[0]))
		return parse_unary(lexp);

	struct expr_lexeme *lex;
	struct expr_token *tok = parse_binary(lexp, prec + 1);
	if (!tok) return NULL;

	for (;;) {
		for (int i = binary_op_prec[prec]; i < binary_op_prec[prec + 1]; i++) {
			if (PARSE_MATCH(binary_op[i].lex)) {
				PARSE_TOK(
					.type = binary_op[i].expr,
					.item[0].expr = tok,
					.item[1].expr = parse_binary(lexp, prec + 1)
				);

				if (!tok->item[1].expr) {
					free_expr_token(tok);
					return NULL;
				}

				goto parse_binary_continue;
			}
		}

		break;

	parse_binary_continue:
		continue;
	}

	return tok;
}

static struct expr_token *parse_expr(struct expr_lexeme **lexp) {
	struct expr_lexeme *lex;
	struct expr_token *tok = parse_binary(lexp, 0);
	if (!tok) return NULL;

	if (PARSE_MATCH(LEX_OPT)) {
		struct expr_token *branch1 = parse_binary(lexp, 0);
		if (!branch1) {
			free_expr_token(tok);
			return NULL;
		}

		if (!PARSE_MATCH(LEX_COLON)) {
			free_expr_token(tok);
			free_expr_token(branch1);
			PARSE_ERR("expected colon for ternary");
			return NULL;
		}

		struct expr_token *branch2 = parse_binary(lexp, 0);
		if (!branch2) {
			free_expr_token(tok);
			free_expr_token(branch1);
			return NULL;
		}

		PARSE_TOK(
			.type = EXPR_TERNARY,
			.item[0].expr = tok,
			.item[1].expr = branch1,
			.item[2].expr = branch2
		);
	}

	return tok;
}

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
