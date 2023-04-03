#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
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
#define LEX_SUBCASE(ch, ty) if (**expr== ch) { LEX_ADD(ty); (*expr)++; break; }
#define LEX_CASE(ch, ty, ...) \
	case ch: \
		__VA_OPT__(LEX_SUBCASE(__VA_ARGS__)) \
		LEX_ADD(ty); break;

static struct expr_lexeme *lex_expr(
	const char **expr,
	const char *term,
	int *line
) {
	size_t tlen = term ? strlen(term) : 0;

	struct expr_lexeme *lex, *lexf, **lexp = &lexf;
	int brackets = 0;

	while (
		**expr &&
		(!term || brackets > 0 || strncmp(*expr, term, tlen) != 0)
	) {
		char c = *((*expr)++);
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

				while ((c = *((*expr)++)) != quote) {
					size_t i = length++;
					contents = realloc(contents, length + 1);
					contents[i] = c;

					if (c == '\\') {
						switch (*((*expr)++)) {
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
								char hi = *((*expr)++);
								char lo = *((*expr)++);

								if (!isxdigit(hi) || !isxdigit(lo)) {
									set_error((lattice_error) {
										.line = *line,
										.code = LATTICE_SYNTAX_ERROR,
										.message = astrdup("invalid hex literal"),
									});

									free(contents);
									free_expr_lexeme(lexf);
									return NULL;
								}

								hi = hi <= '9' ? hi - '0' : 10 + tolower(hi) - 'a';
								lo = lo <= '9' ? lo - '0' : 10 + tolower(lo) - 'a';

								contents[i] = hi << 4 | lo;

								break;

							default:
								set_error((lattice_error) {
									.line = *line,
									.code = LATTICE_SYNTAX_ERROR,
									.message = format("invalid string escape '%c'", *(*expr - 1)),
								});

								free(contents);
								free_expr_lexeme(lexf);
								return NULL;
						}
					}
				}

				contents[length] = 0;

				LEX_ADD(LEX_STRING);
				lex->data.string = contents;

				break;

			default:
				if (isdigit(c)) {
					const char *start = *expr - 1;
					int base = 10;
					double number = (double) (c - '0');

					if (c == '0') {
						switch (**expr) {
							case 'b': base = 2; break;
							case 'o': base = 8; break;
							case 'x': base = 16; break;

							default:
								if (isdigit(**expr)) {
									set_error((lattice_error) {
										.line = *line,
										.code = LATTICE_SYNTAX_ERROR,
										.message = astrdup("decimal literal with leading zero"),
									});

									free_expr_lexeme(lexf);
									return NULL;
								}
						}

						if (base != 10) (*expr)++;
					}

					int dbase = base > 10 ? 10 : base;

					while (
						(base == 16 && isxdigit(**expr)) ||
						('0' <= **expr && **expr < '0' + dbase)
					) {
						number *= base;
						number += **expr <= '9'
							? **expr - '0'
							: 10 + tolower(**expr) - 'a';

						(*expr)++;
					}

					if (base == 10) {
						if (**expr == '.') {
							(*expr)++;

							while (isdigit(**expr)) (*expr)++;
						}

						if (**expr == 'E' || **expr == 'e') {
							(*expr)++;

							if (**expr == '+' || **expr == '-') (*expr)++;

							char *sexpr = *expr;
							while (isdigit(**expr)) (*expr)++;

							if (*expr == sexpr) {
								set_error((lattice_error) {
									.line = *line,
									.code = LATTICE_SYNTAX_ERROR,
									.message = astrdup("exponent cannot be empty"),
								});

								free_expr_lexeme(lexf);
								return NULL;
							}
						}

						char *lit_clone = astrndup(start, *expr - start);
						number = atof(lit_clone);
						free(lit_clone);
					}

					if (!**expr || ispunct(**expr) || isspace(**expr)) {
						LEX_ADD(LEX_NUMBER);
						lex->data.number = number;
					} else {
						set_error((lattice_error) {
							.line = *line,
							.code = LATTICE_SYNTAX_ERROR,
							.message = format("unexpected character '%c'", **expr),
						});

						free_expr_lexeme(lexf);
						return NULL;
					}
				} else if (isalpha(c) || c == '_') {
					const char *start = *expr - 1;
					while (isalnum(**expr) || **expr == '_') (*expr)++;

					char *ident = astrndup(start, *expr - start);
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

					free_expr_lexeme(lexf);
					return NULL;
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

	return lexf;
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

static bool value_truthy(const void *value, lattice_iface iface) {
	switch (iface.type(value)) {
		case LATTICE_TYPE_NULL:
			return false;

		case LATTICE_TYPE_BOOLEAN:
			return iface.value(value).boolean;

		case LATTICE_TYPE_NUMBER:
			return iface.value(value).number != 0.0;

		case LATTICE_TYPE_STRING:
			return iface.value(value).string[0] != 0;

		case LATTICE_TYPE_ARRAY:
		case LATTICE_TYPE_OBJECT:
			return iface.length(value) > 0;
	}
}

static void *eval_expr(
	const struct expr_token *expr,
	const void *ctx,
	lattice_iface iface
) {
	void *lhs, *rhs, *ref;
	lattice_value value = {};
	lattice_index index = {};

	switch (expr->type) {
		case EXPR_NULL:
			return iface.create(LATTICE_TYPE_NULL, value);

		case EXPR_BOOLEAN:
			value.boolean = expr->item[0].boolean;
			return iface.create(LATTICE_TYPE_BOOLEAN, value);

		case EXPR_NUMBER:
			value.number = expr->item[0].number;
			return iface.create(LATTICE_TYPE_NUMBER, value);

		case EXPR_STRING:
			value.string = expr->item[0].string;
			return iface.create(LATTICE_TYPE_STRING, value);

		case EXPR_ARRAY: {}
			void *array = iface.create(LATTICE_TYPE_ARRAY, value);
			for (struct expr_token *v = expr->item[0].expr; v; v = v->next) {
				void *ve = eval_expr(v, ctx, iface);
				if (!ve) {
					iface.free(array);
					return NULL;
				}

				iface.add(array, NULL, ve);
			}

			return array;

		case EXPR_OBJECT: {}
			void *object = iface.create(LATTICE_TYPE_OBJECT, value);
			struct expr_token *v = expr->item[1].expr;
			for (struct expr_token *k = expr->item[0].expr; k; k = k->next) {
				void *ke = eval_expr(k, ctx, iface);
				if (!ke) {
					iface.free(object);
					return NULL;
				}

				switch (iface.type(ke)) {
					case LATTICE_TYPE_NULL:
						iface.free(ke);
						continue;

					case LATTICE_TYPE_STRING: {}
						void *ve = eval_expr(v, ctx, iface);
						if (!ve) {
							iface.free(object); iface.free(ke);
							return NULL;
						}

						iface.add(object, iface.value(ke).string, ve);
						iface.free(ke);
						v = v->next;
						continue;

					default:
						set_error((lattice_error) {
							.line = k->line,
							.code = LATTICE_TYPE_ERROR,
							.message = astrdup("object key must be string or null"),
						});

						iface.free(object); iface.free(ke);
						return NULL;
				}
			}

			return object;

		case EXPR_EITHER:
		case EXPR_BOTH:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			if ((expr->type == EXPR_EITHER) == value_truthy(lhs, iface)) return lhs;

			iface.free(lhs);
			return eval_expr(expr->item[1].expr, ctx, iface);

		case EXPR_EQ:
		case EXPR_NEQ:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			rhs = eval_expr(expr->item[1].expr, ctx, iface);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			bool eq_value = false;
			if (iface.type(lhs) == iface.type(rhs)) {
				switch (iface.type(lhs)) {
					case LATTICE_TYPE_NULL:
						eq_value = true;
						break;

					case LATTICE_TYPE_BOOLEAN:
					case LATTICE_TYPE_NUMBER:
						eq_value = iface.value(lhs).number == iface.value(rhs).number;
						break;

					case LATTICE_TYPE_STRING:
						eq_value = strcmp(iface.value(lhs).string, iface.value(rhs).string) == 0;
						break;

					default:
						break;
				}
			}

			value.boolean = (expr->type == EXPR_EQ) == eq_value;

			iface.free(lhs); iface.free(rhs);
			return iface.create(LATTICE_TYPE_BOOLEAN, value);

		case EXPR_GT:
		case EXPR_LTE:
		case EXPR_LT:
		case EXPR_GTE:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			rhs = eval_expr(expr->item[1].expr, ctx, iface);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			if (iface.type(lhs) != iface.type(rhs)) {
				set_error((lattice_error) {
					.line = expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("can only compare similar types"),
				});

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			if (
				iface.type(lhs) != LATTICE_TYPE_NUMBER &&
				iface.type(lhs) != LATTICE_TYPE_STRING
			) {
				set_error((lattice_error) {
					.line = expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("can only compare number or string"),
				});

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			double cmp = iface.type(lhs) == LATTICE_TYPE_STRING
				? strcmp(iface.value(lhs).string, iface.value(rhs).string)
				: iface.value(lhs).number - iface.value(rhs).number;

			value.boolean = 
				(cmp < 0.0 && (expr->type == EXPR_LT || expr->type == EXPR_LTE)) ||
				(cmp > 0.0 && (expr->type == EXPR_GT || expr->type == EXPR_GTE)) ||
				(cmp == 0.0 && (expr->type == EXPR_LTE || expr->type == EXPR_GTE));

			iface.free(lhs); iface.free(rhs);
			return iface.create(LATTICE_TYPE_BOOLEAN, value);

		case EXPR_ADD:
		case EXPR_SUB:
		case EXPR_MUL:
		case EXPR_DIV:
		case EXPR_QUOT:
		case EXPR_MOD:
		case EXPR_EXP:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			bool bat_lhs_seq =
				iface.type(lhs) == LATTICE_TYPE_STRING ||
				iface.type(lhs) == LATTICE_TYPE_ARRAY;

			if (
				iface.type(lhs) != LATTICE_TYPE_NUMBER &&
				!(bat_lhs_seq && (expr->type == EXPR_ADD || expr->type == EXPR_MUL))
			) {
				set_error((lattice_error) {
					.line = expr->item[0].expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("operands must be numbers"),
				});

				iface.free(lhs);
				return NULL;
			}

			rhs = eval_expr(expr->item[1].expr, ctx, iface);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			if (
				!(bat_lhs_seq && expr->type == EXPR_ADD) &&
				iface.type(rhs) != LATTICE_TYPE_NUMBER
			) {
				set_error((lattice_error) {
					.line = expr->item[1].expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("operands must be numbers"),
				});

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			if (bat_lhs_seq && expr->type == EXPR_ADD) {
				if (iface.type(lhs) != iface.type(rhs)) {
					set_error((lattice_error) {
						.line = expr->line,
						.code = LATTICE_TYPE_ERROR,
						.message = astrdup("sequence concatenation requires similar types"),
					});

					iface.free(lhs); iface.free(rhs);
					return NULL;
				}

				if (iface.type(lhs) == LATTICE_TYPE_STRING) {
					value.string = malloc(iface.length(lhs) + iface.length(rhs) + 1);
					strcpy((char *) value.string, iface.value(lhs).string);
					strcat((char *) value.string, iface.value(rhs).string);

					iface.free(lhs); iface.free(rhs);
					ref = iface.create(LATTICE_TYPE_STRING, value);
					free(value.string);
					return ref;
				} else {
					void *catarray = iface.create(LATTICE_TYPE_ARRAY, value);

					for (size_t i = 0; i < iface.length(lhs); i++) {
						index.array = i;
						iface.add(catarray, NULL, iface.clone(iface.get(lhs, index)));
					}

					for (size_t i = 0; i < iface.length(rhs); i++) {
						index.array = i;
						iface.add(catarray, NULL, iface.clone(iface.get(rhs, index)));
					}

					iface.free(lhs); iface.free(rhs);
					return catarray;
				}
			} else if (bat_lhs_seq && expr->type == EXPR_MUL) {
				if (fmod(iface.value(rhs).number, 1.0) != 0.0) {
					set_error((lattice_error) {
						.line = expr->item[1].expr->line,
						.code = LATTICE_VALUE_ERROR,
						.message = astrdup("sequence multiplication rhs must be whole"),
					});

					iface.free(lhs); iface.free(rhs);
					return NULL;
				}

				size_t n = iface.value(rhs).number;

				if (iface.type(lhs) == LATTICE_TYPE_STRING) {
					value.string = calloc(iface.length(lhs) * n + 1, 1);
					for (size_t i = 0; i < n; i++)
						strcat((char *) value.string, iface.value(lhs).string);

					iface.free(lhs); iface.free(rhs);
					ref = iface.create(LATTICE_TYPE_STRING, value);
					free(value.string);
					return ref;
				} else {
					void *catarray = iface.create(LATTICE_TYPE_ARRAY, value);

					for (size_t i = 0; i < n; i++) {
						for (size_t j = 0; j < iface.length(lhs); j++) {
							index.array = j;
							iface.add(catarray, NULL, iface.clone(iface.get(lhs, index)));
						}
					}

					iface.free(lhs); iface.free(rhs);
					return catarray;
				}
			} else {
				double bat_lhs = iface.value(lhs).number;
				double bat_rhs = iface.value(rhs).number;

				switch (expr->type) {
					case EXPR_ADD:  value.number = bat_lhs + bat_rhs;        break;
					case EXPR_SUB:  value.number = bat_lhs - bat_rhs;        break;
					case EXPR_MUL:  value.number = bat_lhs * bat_rhs;        break;
					case EXPR_DIV:  value.number = bat_lhs / bat_rhs;        break;
					case EXPR_QUOT: value.number = floor(bat_lhs / bat_rhs); break;
					case EXPR_MOD:  value.number = fmod(bat_lhs, bat_rhs);   break;
					case EXPR_EXP:  value.number = pow(bat_lhs, bat_rhs);    break;
					default:        break;
				}

				iface.free(lhs); iface.free(rhs);
				return iface.create(LATTICE_TYPE_NUMBER, value);
			}

		case EXPR_AND:
		case EXPR_OR:
		case EXPR_XOR:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			if (iface.type(lhs) != LATTICE_TYPE_NUMBER) {
				set_error((lattice_error) {
					.line = expr->item[0].expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("bitwise operands must be numbers"),
				});

				iface.free(lhs);
				return NULL;
			}

			if (fmod(iface.value(lhs).number, 1.0) != 0.0) {
				set_error((lattice_error) {
					.line = expr->item[0].expr->line,
					.code = LATTICE_VALUE_ERROR,
					.message = astrdup("bitwise operands must be whole numbers"),
				});

				iface.free(lhs);
				return NULL;
			}

			rhs = eval_expr(expr->item[1].expr, ctx, iface);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			if (iface.type(rhs) != LATTICE_TYPE_NUMBER) {
				set_error((lattice_error) {
					.line = expr->item[1].expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("bitwise operands must be numbers"),
				});

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			if (fmod(iface.value(rhs).number, 1.0) != 0.0) {
				set_error((lattice_error) {
					.line = expr->item[1].expr->line,
					.code = LATTICE_VALUE_ERROR,
					.message = astrdup("bitwise operands must be whole numbers"),
				});

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			uint64_t bbw_lhs = iface.value(lhs).number;
			uint64_t bbw_rhs = iface.value(rhs).number;

			uint64_t bbw_value;
			switch (expr->type) {
				case EXPR_AND: bbw_value = bbw_lhs & bbw_rhs; break;
				case EXPR_OR:  bbw_value = bbw_lhs | bbw_rhs; break;
				case EXPR_XOR: bbw_value = bbw_lhs ^ bbw_rhs; break;
				default:       return NULL;
			}

			iface.free(lhs); iface.free(rhs);

			lattice_value bbw_number = { .number = (double) bbw_value };
			return iface.create(LATTICE_TYPE_NUMBER, bbw_number);

		case EXPR_COMP:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			if (iface.type(lhs) != LATTICE_TYPE_NUMBER) {
				set_error((lattice_error) {
					.line = expr->item[0].expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("bitwise operands must be numbers"),
				});

				iface.free(lhs);
				return NULL;
			}

			if (fmod(iface.value(lhs).number, 1.0) != 0.0) {
				set_error((lattice_error) {
					.line = expr->item[0].expr->line,
					.code = LATTICE_VALUE_ERROR,
					.message = astrdup("bitwise operands must be whole numbers"),
				});

				iface.free(lhs);
				return NULL;
			}

			uint64_t comp_int = iface.value(lhs).number;
			iface.free(lhs);

			lattice_value comp_number = { .number = (double) (~comp_int) };
			return iface.create(LATTICE_TYPE_NUMBER, comp_number);

		case EXPR_NOT:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			bool not_value = !value_truthy(lhs, iface);
			iface.free(lhs);

			lattice_value not_boolean = { .boolean = not_value };
			return iface.create(LATTICE_TYPE_BOOLEAN, not_boolean);

		case EXPR_NEG:
		case EXPR_POS:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			if (iface.type(lhs) != LATTICE_TYPE_NUMBER) {
				set_error((lattice_error) {
					.line = expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("operand must be number"),
				});

				iface.free(lhs);
				return NULL;
			}

			double np_value = iface.value(lhs).number;
			iface.free(lhs);

			return iface.create(LATTICE_TYPE_NUMBER, (lattice_value) {
				.number = expr->type == EXPR_NEG ? -np_value : np_value
			});

		case EXPR_ROOT:
			return iface.clone(ctx);

		case EXPR_IDENT:
		case EXPR_LOOKUP:
			if (expr->type == EXPR_IDENT) lhs = (void *) ctx;
			else lhs = eval_expr(expr->item[0].expr, ctx, iface);

			if (!lhs) return NULL;
			if (iface.type(lhs) != LATTICE_TYPE_OBJECT) {
				set_error((lattice_error) {
					.line = expr->line,
					.code = LATTICE_TYPE_ERROR,
					.message = astrdup("can only lookup properties of object"),
				});

				if (expr->type == EXPR_LOOKUP) iface.free(lhs);
				return NULL;
			}

			index.object = expr->item[expr->type == EXPR_IDENT ? 0 : 1].ident;
			void *value = iface.get(lhs, index);

			if (!value) {
				set_error((lattice_error) {
					.line = expr->line,
					.code = LATTICE_NAME_ERROR,
					.message = format(
						"'%s' is undefined",
						expr->item[expr->type == EXPR_IDENT ? 0 : 1].ident
					),
				});

				if (expr->type == EXPR_LOOKUP) iface.free(lhs);
				return NULL;
			}

			value = iface.clone(value);
			if (expr->type == EXPR_LOOKUP) iface.free(lhs);
			return value;

		case EXPR_METHOD:
			// TODO
			return NULL;

		case EXPR_INDEX:
			lhs = eval_expr(expr->item[0].expr, ctx, iface);
			if (!lhs) return NULL;

			rhs = eval_expr(expr->item[1].expr, ctx, iface);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			switch (iface.type(lhs)) {
				case LATTICE_TYPE_STRING:
				case LATTICE_TYPE_ARRAY:
					if (iface.type(rhs) != LATTICE_TYPE_NUMBER) {
						set_error((lattice_error) {
							.line = expr->item[1].expr->line,
							.code = LATTICE_TYPE_ERROR,
							.message = astrdup("index must be a number"),
						});

						iface.free(lhs); iface.free(rhs);
						return NULL;
					}

					if (fmod(iface.value(rhs).number, 1.0) != 0.0) {
						set_error((lattice_error) {
							.line = expr->item[1].expr->line,
							.code = LATTICE_VALUE_ERROR,
							.message = astrdup("indices must be whole numbers"),
						});

						iface.free(lhs); iface.free(rhs);
						return NULL;
					}

					size_t i = (size_t) iface.value(rhs).number;
					if (i >= iface.length(lhs)) {
						set_error((lattice_error) {
							.line = expr->item[1].expr->line,
							.code = LATTICE_VALUE_ERROR,
							.message = astrdup("index out of range"),
						});

						iface.free(lhs); iface.free(rhs);
						return NULL;
					}

					if (iface.type(lhs) == LATTICE_TYPE_STRING) {
						char *string = calloc(2, sizeof(char));
						string[0] = iface.value(lhs).string[i];

						iface.free(lhs); iface.free(rhs);
						return iface.create(
							LATTICE_TYPE_STRING,
							(lattice_value) { .string = string }
						);
					} else {
						index.array = i;
						void *value = iface.get(lhs, index);

						iface.free(lhs); iface.free(rhs);
						return iface.clone(value);
					}

				case LATTICE_TYPE_OBJECT:
					if (iface.type(rhs) != LATTICE_TYPE_STRING) {
						set_error((lattice_error) {
							.line = expr->item[1].expr->line,
							.code = LATTICE_TYPE_ERROR,
							.message = astrdup("index must be a string"),
						});

						iface.free(lhs); iface.free(rhs);
						return NULL;
					}

					index.object = iface.value(rhs).string;
					void *value = iface.clone(iface.get(lhs, index));
					iface.free(lhs); iface.free(rhs);

					if (!value) {
						set_error((lattice_error) {
							.line = expr->item[1].expr->line,
							.code = LATTICE_VALUE_ERROR,
							.message = astrdup("index out of range"),
						});

						iface.free(value);
						return NULL;
					}

					return value;

				default:
					set_error((lattice_error) {
						.line = expr->line,
						.code = LATTICE_TYPE_ERROR,
						.message = astrdup("can only index string, array, or object"),
					});

					iface.free(lhs);
					return NULL;
			}

		case EXPR_TERNARY: {}
			void *cond_value = eval_expr(expr->item[0].expr, ctx, iface);
			if (!cond_value) return NULL;
			bool cond = value_truthy(cond_value, iface);

			iface.free(cond_value);
			return eval_expr(expr->item[cond ? 1 : 2].expr, ctx, iface);
	}
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
