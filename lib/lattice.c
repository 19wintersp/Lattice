#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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

void lattice_error_free(lattice_error *err) {
	if (err) {
		free(err->file);
		free(err->message);
		free(err);
	}
}

static lattice_error *set_error(
	lattice_error **errp,
	int line,
	enum lattice_error_code code,
	const char *message
) {
	if (errp) {
		*errp = malloc(sizeof(lattice_error));
		**errp = (lattice_error) {
			.line = line,
			.code = code,
			.message = astrdup(message),
		};

		return *errp;
	} else {
		return NULL;
	}
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
	if (!token) return;

	free_token(token->child);
	free_token(token->next);

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
	int *line,
	lattice_error **errp
) {
	size_t tlen = term ? strlen(term) : 0;

	struct expr_lexeme *lex = NULL, *lexf = NULL, **lexp = &lexf;
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
									set_error(errp, *line,
										LATTICE_SYNTAX_ERROR, "invalid hex literal");

									free(contents);
									free_expr_lexeme(lexf);
									return NULL;
								}

								hi = hi <= '9' ? hi - '0' : 10 + tolower(hi) - 'a';
								lo = lo <= '9' ? lo - '0' : 10 + tolower(lo) - 'a';

								contents[i] = hi << 4 | lo;

								break;

							default:
								set_error(errp, *line,
									LATTICE_SYNTAX_ERROR, "invalid string escape");

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
									set_error(errp, *line,
										LATTICE_SYNTAX_ERROR, "decimal literal with leading zero");

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

							const char *sexpr = *expr;
							while (isdigit(**expr)) (*expr)++;

							if (*expr == sexpr) {
								set_error(errp, *line,
									LATTICE_SYNTAX_ERROR, "exponent cannot be empty");

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
						set_error(errp, *line,
							LATTICE_SYNTAX_ERROR, "unexpected character");

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
					set_error(errp, *line,
						LATTICE_SYNTAX_ERROR, "unexpected character");

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

static struct expr_token *parse_ternary(
	struct expr_lexeme **lexp,
	lattice_error **errp
);

static struct expr_token *parse_primary(
	struct expr_lexeme **lexp,
	lattice_error **errp
) {
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
		tok = parse_ternary(lexp, errp);
		if (!tok || PARSE_MATCH(LEX_RPAREN)) return tok;

		set_error(errp, lex->line,
			LATTICE_SYNTAX_ERROR, "expected closing parenthesis after group");

		free_expr_token(tok);
		return NULL;
	}

	if (PARSE_MATCH(LEX_LBRACK)) {
		struct expr_token *value = NULL, *last, *next;
		if (!PARSE_MATCH(LEX_RBRACK)) {
			do {
				next = parse_ternary(lexp, errp);
				if (!next) return NULL;

				if (value) {
					last->next = next;
					last = next;
				} else {
					value = last = next;
				}
			} while (PARSE_MATCH(LEX_COMMA));

			if (!PARSE_MATCH(LEX_RBRACK)) {
				set_error(errp, lex->line,
					LATTICE_SYNTAX_ERROR, "expected closing bracket after array values");

				free_expr_token(value);
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
				nextk = parse_ternary(lexp, errp);
				if (!nextk) return NULL;

				if (key) {
					lastk->next = nextk;
					lastk = nextk;
				} else {
					key = lastk = nextk;
				}

				if (!PARSE_MATCH(LEX_COLON)) {
					set_error(errp, lex->line,
						LATTICE_SYNTAX_ERROR, "expected colon after object key");

					free_expr_token(key);
					free_expr_token(value);
					return NULL;
				}

				nextv = parse_ternary(lexp, errp);
				if (!nextv) return NULL;

				if (value) {
					lastv->next = nextv;
					lastv = nextv;
				} else {
					value = lastv = nextv;
				}
			} while (PARSE_MATCH(LEX_COMMA));

			if (!PARSE_MATCH(LEX_RBRACE)) {
				set_error(errp, lex->line,
					LATTICE_SYNTAX_ERROR, "expected closing brace after object entries");

				free_expr_token(key);
				free_expr_token(value);
				return NULL;
			}
		}

		PARSE_TOK(.type = EXPR_OBJECT, .item[0].expr = key, .item[1].expr = value);
		return tok;
	}

	if ((lex = *lexp))
		set_error(errp, lex->line, LATTICE_SYNTAX_ERROR, "expected expression");
	else
		set_error(errp, 0, LATTICE_SYNTAX_ERROR, "unexpected end of file");

	return NULL;
}

static struct expr_token *parse_call(
	struct expr_lexeme **lexp,
	lattice_error **errp
) {
	struct expr_lexeme *lex;
	struct expr_token *tok = parse_primary(lexp, errp);
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
							next = parse_ternary(lexp, errp);
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
							set_error(errp, lex->line,
								LATTICE_SYNTAX_ERROR, "expected closing parenthesis after arguments");

							free_expr_token(tok);
							free_expr_token(arg);
							return NULL;
						}
					}

					tok->type = EXPR_METHOD;
					tok->item[2].expr = arg;
				}
			} else {
				set_error(errp, lex->line,
					LATTICE_SYNTAX_ERROR, "expected identifier after dot");

				free_expr_token(tok);
				return NULL;
			}
		} else if (PARSE_MATCH(LEX_LBRACK)) {
			struct expr_token *index = parse_ternary(lexp, errp), *index2 = NULL;
			if (!index) {
				free_expr_token(tok);
				return NULL;
			}

			if (PARSE_MATCH(LEX_COMMA)) {
				index2 = parse_ternary(lexp, errp);
				if (!index2) {
					free_expr_token(tok);
					free_expr_token(index);
					return NULL;
				}
			}

			if (!PARSE_MATCH(LEX_RBRACK)) {
				set_error(errp, lex->line,
					LATTICE_SYNTAX_ERROR, "expected closing bracket after subscription");

				free_expr_token(tok);
				free_expr_token(index);
				free_expr_token(index2);
				return NULL;
			}

			PARSE_TOK(
				.type = EXPR_INDEX,
				.item[0].expr = tok,
				.item[1].expr = index,
				.item[2].expr = index2,
			);
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

static struct expr_token *parse_unary(
	struct expr_lexeme **lexp,
	lattice_error **errp
) {
	struct expr_lexeme *lex;
	struct expr_token *tok;

	for (size_t i = 0; i < sizeof(unary_op) / sizeof(unary_op[0]); i++) {
		if (PARSE_MATCH(unary_op[i].lex)) {
			tok = parse_unary(lexp, errp);
			if (!tok) return NULL;

			PARSE_TOK(.type = unary_op[i].expr, .item[0].expr = tok);
			return tok;
		}
	}

	return parse_call(lexp, errp);
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

static struct expr_token *parse_binary(
	struct expr_lexeme **lexp, size_t prec,
	lattice_error **errp
) {
	if (prec >= sizeof(binary_op_prec) / sizeof(binary_op_prec[0]))
		return parse_unary(lexp, errp);

	struct expr_lexeme *lex;
	struct expr_token *tok = parse_binary(lexp, prec + 1, errp);
	if (!tok) return NULL;

	for (;;) {
		for (int i = binary_op_prec[prec]; i < binary_op_prec[prec + 1]; i++) {
			if (PARSE_MATCH(binary_op[i].lex)) {
				PARSE_TOK(
					.type = binary_op[i].expr,
					.item[0].expr = tok,
					.item[1].expr = parse_binary(lexp, prec + 1, errp)
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

static struct expr_token *parse_ternary(
	struct expr_lexeme **lexp,
	lattice_error **errp
) {
	struct expr_lexeme *lex;
	struct expr_token *tok = parse_binary(lexp, 0, errp);
	if (!tok) return NULL;

	if (PARSE_MATCH(LEX_OPT)) {
		struct expr_token *branch1 = parse_binary(lexp, 0, errp);
		if (!branch1) {
			free_expr_token(tok);
			return NULL;
		}

		if (!PARSE_MATCH(LEX_COLON)) {
			set_error(errp, lex->line,
				LATTICE_SYNTAX_ERROR, "expected colon for ternary");

			free_expr_token(tok);
			free_expr_token(branch1);
			return NULL;
		}

		struct expr_token *branch2 = parse_binary(lexp, 0, errp);
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

static struct expr_token *parse_expr(
	const char **expr,
	const char *term,
	int *line,
	lattice_error **errp
) {
	struct expr_lexeme *expr_lex = lex_expr(expr, term, line, errp);

	if (!expr_lex) return NULL;

	struct expr_lexeme *expr_lext = expr_lex;
	struct expr_token *expr_tok = parse_ternary(&expr_lext, errp);

	if (expr_lext) {
		set_error(errp, *line, LATTICE_SYNTAX_ERROR, "extra tokens in expression");

		free_expr_lexeme(expr_lex);
		free_expr_token(expr_tok);
		return NULL;
	}

	free_expr_lexeme(expr_lex);

	if (!expr_tok)
		set_error(errp, *line,
			LATTICE_SYNTAX_ERROR, "unterminated expression in substitution");

	return expr_tok;
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

static bool value_eq(const void *lhs, const void *rhs, lattice_iface iface) {
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

	return eq_value;
}

static char hash_trans[32] = "UTQFHY[J^]LVZCAD@S\\WXGPINEOBKR_M";
uint8_t hash(const char *str) {
	uint8_t s1 = 0, s2 = 0;

	for (const char *s = str; *str; str++) {
		if ((s - str) & 1)
			s1 = hash_trans[s1 ^ (*str & 0x1f)] & 0x1f;
		else
			s2 = hash_trans[s2 ^ (*str & 0x1f)] & 0x1f;
	}

	return s1 ^ (s2 << 3);
}

enum method_id {
	METHOD_BOOLEAN  = 0xad,
	METHOD_CONTAINS = 0x31,
	METHOD_FIND     = 0x3d,
	METHOD_FMT_TIME = 0xa4,
	METHOD_JOIN     = 0xc4,
	METHOD_KEYS     = 0x2c,
	METHOD_LENGTH   = 0xd9,
	METHOD_LOWER    = 0x8c,
	METHOD_NAN      = 0x34,
	METHOD_NUMBER   = 0x7e,
	METHOD_REAL     = 0x97,
	METHOD_REPEAT   = 0x1d,
	METHOD_REPLACE  = 0x58,
	METHOD_REVERSE  = 0x76,
	METHOD_ROUND    = 0x24,
	METHOD_SORT     = 0xc8,
	METHOD_STRING   = 0x50,
	METHOD_TYPE     = 0xe5,
	METHOD_UPPER    = 0x09,
	METHOD_VALUES   = 0x02,
};

static struct method {
	const char *name;
	uint8_t length;
} methods[256] = {
	[METHOD_BOOLEAN]  = { "boolean",  0 },
	[METHOD_CONTAINS] = { "contains", 1 },
	[METHOD_FIND]     = { "find",     1 },
	[METHOD_FMT_TIME] = { "datetime", 0 },
	[METHOD_JOIN]     = { "join",     1 },
	[METHOD_KEYS]     = { "keys",     0 },
	[METHOD_LENGTH]   = { "length",   0 },
	[METHOD_LOWER]    = { "lower",    0 },
	[METHOD_NAN]      = { "nan",      0 },
	[METHOD_NUMBER]   = { "number",   0 },
	[METHOD_REAL]     = { "real",     0 },
	[METHOD_REPEAT]   = { "repeat",   1 },
	[METHOD_REPLACE]  = { "replace",  2 },
	[METHOD_REVERSE]  = { "reverse",  0 },
	[METHOD_ROUND]    = { "round",    0 },
	[METHOD_SORT]     = { "sort",     0 },
	[METHOD_STRING]   = { "string",   0 },
	[METHOD_TYPE]     = { "type",     0 },
	[METHOD_UPPER]    = { "upper",    0 },
	[METHOD_VALUES]   = { "values",   0 },
};

static const char *types[] = {
	[LATTICE_TYPE_NULL]    = "null",
	[LATTICE_TYPE_BOOLEAN] = "boolean",
	[LATTICE_TYPE_NUMBER]  = "number",
	[LATTICE_TYPE_STRING]  = "string",
	[LATTICE_TYPE_ARRAY]   = "array",
	[LATTICE_TYPE_OBJECT]  = "object",
};

void *method(
	const char *name,
	const void *this,
	const void *args[],
	lattice_iface iface,
	lattice_error **errp,
	int line
) {
	uint8_t id = hash(name);
	struct method method = methods[id];

	if (!method.name || strcmp(method.name, name) != 0)
		return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

	size_t n = 0;
	for (const void **arg = args; *arg; arg++) n++;

	if (n != method.length) {
		set_error(errp, line, LATTICE_VALUE_ERROR, n > method.length
			? "too many arguments to method"
			: "not enough arguments to method");
		return NULL;
	}

	void *value;
	switch ((enum method_id) id) {
		case METHOD_BOOLEAN:
			return iface.create(
				LATTICE_TYPE_BOOLEAN,
				(lattice_value) { .boolean = value_truthy(this, iface) }
			);

		case METHOD_CONTAINS:
		case METHOD_FIND:
			if (iface.type(this) < LATTICE_TYPE_STRING)
				return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

			const char *this_str = NULL, *search_str;
			size_t search_len;

			if (iface.type(this) == LATTICE_TYPE_STRING) {
				if (iface.type(args[0] != LATTICE_TYPE_STRING))
					return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

				this_str = iface.value(this).string;
				search_str = iface.value(args[0]).string;
				search_len = strlen(search_str);
			}

			double in = -1;
			for (size_t i = 0; i < iface.length(this); i++) {
				if (
					this_str
						? strncmp(this_str + i, search_str, search_len)
						: value_eq(
							iface.get(this, (lattice_index) { .array = i }), args[0], iface
						)
				) {
					in = i;
					break;
				}
			}

			return id == METHOD_CONTAINS
				? iface.create(LATTICE_TYPE_BOOLEAN, (lattice_value) { .boolean = in >= 0 })
				: iface.create(LATTICE_TYPE_NUMBER, (lattice_value) { .number = in });

		case METHOD_FMT_TIME:
			if (iface.type(this) != LATTICE_TYPE_STRING)
				return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

			time_t timer = time(NULL);
			char fmt_time[1024] = {};
			strftime(fmt_time, 1024, iface.value(this).string, localtime(&timer));

			return iface.create(
				LATTICE_TYPE_STRING,
				(lattice_value) { .string = fmt_time }
			);

		case METHOD_JOIN:
			if (
				iface.type(this) != LATTICE_TYPE_ARRAY ||
				iface.type(args[0]) != LATTICE_TYPE_STRING
			) return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

			const char *joiner = iface.value(args[0]).string;
			size_t count = iface.length(this), length = (count - 1) * strlen(joiner);

			for (size_t i = 0; i < count; i++) {
				const void *item = iface.get(this, (lattice_index) { .array = i });
				if (iface.type(item) != LATTICE_TYPE_STRING)
					return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

				length += strlen(iface.value(item).string);
			}

			char *joined = malloc(length + 1);
			joined[0] = 0;

			for (size_t i = 0; i < count; i++) {
				const void *item = iface.get(this, (lattice_index) { .array = i });
				strcat(joined, iface.value(item).string);

				if (i + 1 < count) strcat(joined, joiner);
			}

			value = iface.create(
				LATTICE_TYPE_STRING,
				(lattice_value) { .string = joined }
			);

			free(joined);
			return value;

		case METHOD_KEYS:
		case METHOD_VALUES:
			if (iface.type(this) < LATTICE_TYPE_ARRAY)
				return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

			value = iface.create(LATTICE_TYPE_ARRAY, (lattice_value) {});
			size_t keys_length = iface.length(this);
			const char **keys = NULL;

			if (iface.type(this) == LATTICE_TYPE_OBJECT) {
				keys = malloc(sizeof(const char *) * keys_length);
				iface.keys(this, keys);
			}

			lattice_type key_type = keys ? LATTICE_TYPE_STRING : LATTICE_TYPE_NUMBER;
			for (size_t i = 0; i < keys_length; i++) {
				double in = i;
				const char *key = keys ? (void *) keys[i] : *((void **) &in);

				iface.add(value, NULL, id == METHOD_KEYS
					? iface.create(key_type, (lattice_value) key)
					: iface.clone(iface.get(this, (lattice_index) key))
				);
			}

			free(keys);
			return value;

		case METHOD_LENGTH:
			return iface.type(this) < LATTICE_TYPE_STRING
				? iface.create(LATTICE_TYPE_NULL, (lattice_value) {})
				: iface.create(
					LATTICE_TYPE_NUMBER,
					(lattice_value) { .number = (double) iface.length(this) }
				);

		case METHOD_LOWER:
		case METHOD_UPPER:
			if (iface.type(this) != LATTICE_TYPE_STRING)
				return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

			char *buf = malloc(iface.length(this) + 1);
			strcpy(buf, iface.value(this).string);

			for (size_t i = 0; buf[i]; i++)
				buf[i] = (char) (id == METHOD_LOWER ? tolower : toupper)(buf[i]);

			value = iface.create(LATTICE_TYPE_STRING, (lattice_value) { .string = buf });

			free(buf);
			return value;

		case METHOD_NAN:
			return iface.type(this) != LATTICE_TYPE_NUMBER
				? iface.create(LATTICE_TYPE_NULL, (lattice_value) {})
				: iface.create(
					LATTICE_TYPE_BOOLEAN,
					(lattice_value) { .boolean = isnan(iface.value(this).number) }
				);

		case METHOD_NUMBER: {}
			double n;
			switch (iface.type(this)) {
				case LATTICE_TYPE_NULL:
					n = 0;
					break;

				case LATTICE_TYPE_BOOLEAN:
					n = iface.value(this).boolean;
					break;

				case LATTICE_TYPE_NUMBER:
					n = iface.value(this).number;
					break;

				case LATTICE_TYPE_STRING:
					n = atof(iface.value(this).string);
					break;

				default:
					return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});
			}

			return iface.create(LATTICE_TYPE_NUMBER, (lattice_value) { .number = n });

		case METHOD_REAL:
			return iface.type(this) != LATTICE_TYPE_NUMBER
				? iface.create(LATTICE_TYPE_NULL, (lattice_value) {})
				: iface.create(
					LATTICE_TYPE_BOOLEAN,
					(lattice_value) { .boolean = isfinite(iface.value(this).number) }
				);

		case METHOD_REPEAT:
			if (iface.type(args[0]) != LATTICE_TYPE_NUMBER)
				return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});

			size_t repand = iface.length(this), repeat = iface.value(args[0]).number;
			switch (iface.type(this)) {
				case LATTICE_TYPE_STRING: {}
					const char *src = iface.value(this).string;
					char *new = malloc(repand * repeat + 1), *cur = new;

					new[0] = 0;
					for (size_t i = 0; i < repeat; i++, cur += repand) strcpy(cur, src);

					value = iface.create(
						LATTICE_TYPE_STRING,
						(lattice_value) { .string = new }
					);
					free(new);
					return value;

				case LATTICE_TYPE_ARRAY:
					value = iface.create(LATTICE_TYPE_ARRAY, (lattice_value) {});
					for (size_t i = 0; i < repeat; i++)
						for (size_t j = 0; j < repand; j++)
							iface.add(
								value, NULL,
								iface.clone(iface.get(this, (lattice_index) { .array = j }))
							);

					return value;

				default:
					return iface.create(LATTICE_TYPE_NULL, (lattice_value) {});
			}

		case METHOD_REPLACE:
		case METHOD_REVERSE:
			return iface.create(LATTICE_TYPE_NULL, (lattice_value) {}); // todo

		case METHOD_ROUND:
			return iface.type(this) != LATTICE_TYPE_NUMBER
				? iface.create(LATTICE_TYPE_NULL, (lattice_value) {})
				: iface.create(
					LATTICE_TYPE_NUMBER,
					(lattice_value) { .number = round(iface.value(this).number) }
				);

		case METHOD_SORT:
			return iface.create(LATTICE_TYPE_NULL, (lattice_value) {}); // todo

		case METHOD_STRING: {}
			void *json = iface.print(this);
			value = iface.create(LATTICE_TYPE_STRING, (lattice_value) { .string = json });

			free(json);
			return value;

		case METHOD_TYPE:
			return iface.create(
				LATTICE_TYPE_STRING,
				(lattice_value) { .string = types[iface.type(this)] }
			);
	}
}

static void *eval_expr(
	const struct expr_token *expr,
	const void *ctx,
	lattice_iface iface,
	lattice_error **errp
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
				void *ve = eval_expr(v, ctx, iface, errp);
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
				void *ke = eval_expr(k, ctx, iface, errp);
				if (!ke) {
					iface.free(object);
					return NULL;
				}

				switch (iface.type(ke)) {
					case LATTICE_TYPE_NULL:
						iface.free(ke);
						continue;

					case LATTICE_TYPE_STRING: {}
						void *ve = eval_expr(v, ctx, iface, errp);
						if (!ve) {
							iface.free(object); iface.free(ke);
							return NULL;
						}

						iface.add(object, iface.value(ke).string, ve);
						iface.free(ke);
						v = v->next;
						continue;

					default:
						set_error(errp, k->line,
							LATTICE_TYPE_ERROR, "object key must be string or null");

						iface.free(object); iface.free(ke);
						return NULL;
				}
			}

			return object;

		case EXPR_EITHER:
		case EXPR_BOTH:
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			if ((expr->type == EXPR_EITHER) == value_truthy(lhs, iface)) return lhs;

			iface.free(lhs);
			return eval_expr(expr->item[1].expr, ctx, iface, errp);

		case EXPR_EQ:
		case EXPR_NEQ:
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			rhs = eval_expr(expr->item[1].expr, ctx, iface, errp);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			value.boolean = (expr->type == EXPR_EQ) == value_eq(lhs, rhs, iface);

			iface.free(lhs); iface.free(rhs);
			return iface.create(LATTICE_TYPE_BOOLEAN, value);

		case EXPR_GT:
		case EXPR_LTE:
		case EXPR_LT:
		case EXPR_GTE:
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			rhs = eval_expr(expr->item[1].expr, ctx, iface, errp);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			if (iface.type(lhs) != iface.type(rhs)) {
				set_error(errp, expr->line,
					LATTICE_TYPE_ERROR, "can only compare similar types");

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			if (
				iface.type(lhs) != LATTICE_TYPE_NUMBER &&
				iface.type(lhs) != LATTICE_TYPE_STRING
			) {
				set_error(errp, expr->line,
					LATTICE_TYPE_ERROR, "can only compare number or string");

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
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			bool bat_lhs_seq =
				iface.type(lhs) == LATTICE_TYPE_STRING ||
				iface.type(lhs) == LATTICE_TYPE_ARRAY;

			if (
				iface.type(lhs) != LATTICE_TYPE_NUMBER &&
				!(bat_lhs_seq && (expr->type == EXPR_ADD || expr->type == EXPR_MUL))
			) {
				set_error(errp, expr->item[0].expr->line,
					LATTICE_TYPE_ERROR, "operands must be numbers");

				iface.free(lhs);
				return NULL;
			}

			rhs = eval_expr(expr->item[1].expr, ctx, iface, errp);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			if (
				!(bat_lhs_seq && expr->type == EXPR_ADD) &&
				iface.type(rhs) != LATTICE_TYPE_NUMBER
			) {
				set_error(errp, expr->item[1].expr->line,
					LATTICE_TYPE_ERROR, "operands must be numbers");

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			if (bat_lhs_seq && expr->type == EXPR_ADD) {
				if (iface.type(lhs) != iface.type(rhs)) {
					set_error(errp, expr->line,
						LATTICE_TYPE_ERROR, "sequence concatenation requires similar types");

					iface.free(lhs); iface.free(rhs);
					return NULL;
				}

				if (iface.type(lhs) == LATTICE_TYPE_STRING) {
					value.string = malloc(iface.length(lhs) + iface.length(rhs) + 1);
					strcpy((char *) value.string, iface.value(lhs).string);
					strcat((char *) value.string, iface.value(rhs).string);

					iface.free(lhs); iface.free(rhs);
					ref = iface.create(LATTICE_TYPE_STRING, value);
					free((char *) value.string);
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
					set_error(errp, expr->item[1].expr->line,
						LATTICE_VALUE_ERROR, "sequence multiplication rhs must be whole");

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
					free((char *) value.string);
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
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			if (iface.type(lhs) != LATTICE_TYPE_NUMBER) {
				set_error(errp, expr->item[0].expr->line,
					LATTICE_TYPE_ERROR, "bitwise operands must be numbers");

				iface.free(lhs);
				return NULL;
			}

			if (fmod(iface.value(lhs).number, 1.0) != 0.0) {
				set_error(errp, expr->item[0].expr->line,
					LATTICE_VALUE_ERROR, "bitwise operands must be whole numbers");

				iface.free(lhs);
				return NULL;
			}

			rhs = eval_expr(expr->item[1].expr, ctx, iface, errp);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			if (iface.type(rhs) != LATTICE_TYPE_NUMBER) {
				set_error(errp, expr->item[1].expr->line,
					LATTICE_TYPE_ERROR, "bitwise operands must be numbers");

				iface.free(lhs); iface.free(rhs);
				return NULL;
			}

			if (fmod(iface.value(rhs).number, 1.0) != 0.0) {
				set_error(errp, expr->item[1].expr->line,
					LATTICE_VALUE_ERROR, "bitwise operands must be whole numbers");

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
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			if (iface.type(lhs) != LATTICE_TYPE_NUMBER) {
				set_error(errp, expr->item[0].expr->line,
					LATTICE_TYPE_ERROR, "bitwise operands must be numbers");

				iface.free(lhs);
				return NULL;
			}

			if (fmod(iface.value(lhs).number, 1.0) != 0.0) {
				set_error(errp, expr->item[0].expr->line,
					LATTICE_VALUE_ERROR, "bitwise operands must be whole numbers");

				iface.free(lhs);
				return NULL;
			}

			uint64_t comp_int = iface.value(lhs).number;
			iface.free(lhs);

			lattice_value comp_number = { .number = (double) (~comp_int) };
			return iface.create(LATTICE_TYPE_NUMBER, comp_number);

		case EXPR_NOT:
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			bool not_value = !value_truthy(lhs, iface);
			iface.free(lhs);

			lattice_value not_boolean = { .boolean = not_value };
			return iface.create(LATTICE_TYPE_BOOLEAN, not_boolean);

		case EXPR_NEG:
		case EXPR_POS:
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			if (iface.type(lhs) != LATTICE_TYPE_NUMBER) {
				set_error(errp, expr->line,
					LATTICE_TYPE_ERROR, "operand must be number");

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
			else lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);

			if (!lhs) return NULL;
			if (iface.type(lhs) != LATTICE_TYPE_OBJECT) {
				set_error(errp, expr->line,
					LATTICE_TYPE_ERROR, "can only lookup properties of object");

				if (expr->type == EXPR_LOOKUP) iface.free(lhs);
				return NULL;
			}

			index.object = expr->item[expr->type == EXPR_IDENT ? 0 : 1].ident;
			void *value = iface.get(lhs, index);

			if (!value) {
				lattice_error *err = set_error(errp, expr->line, LATTICE_NAME_ERROR, "");
				if (err) {
					free(err->message);
					err->message = format(
						"'%s' is undefined",
						expr->item[expr->type == EXPR_IDENT ? 0 : 1].ident
					);
				}

				if (expr->type == EXPR_LOOKUP) iface.free(lhs);
				return NULL;
			}

			value = iface.clone(value);
			if (expr->type == EXPR_LOOKUP) iface.free(lhs);
			return value;

		case EXPR_METHOD: {}
			if (!(lhs = eval_expr(expr->item[0].expr, ctx, iface, errp)))
				return NULL;

			size_t n = 0, i = 0;
			for (struct expr_token *arg = expr->item[2].expr; arg; arg = arg->next) n++;

			void **args = malloc(sizeof(void *) * (n + 1));
			args[n] = NULL;

			for (struct expr_token *arg = expr->item[2].expr; arg; arg = arg->next) {
				if (!(args[i++] = eval_expr(arg, ctx, iface, errp))) {
					iface.free(lhs);
					for (size_t j = 0; j < i - 1; j++) iface.free(args[j]);
					return NULL;
				}
			}

			value = method(
				expr->item[1].ident, lhs, (const void **) args, iface, errp, expr->line);

			iface.free(lhs);
			for (size_t j = 0; j < n; j++) iface.free(args[j]);
			free(args);
			return value;

		case EXPR_INDEX:
			lhs = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!lhs) return NULL;

			rhs = eval_expr(expr->item[1].expr, ctx, iface, errp);
			if (!rhs) {
				iface.free(lhs);
				return NULL;
			}

			void *rhs2 = expr->item[2].expr
				? eval_expr(expr->item[2].expr, ctx, iface, errp)
				: NULL;
			if (expr->item[2].expr && !rhs2) {
				iface.free(lhs);
				iface.free(rhs);
				return NULL;
			}

			switch (iface.type(lhs)) {
				case LATTICE_TYPE_STRING:
				case LATTICE_TYPE_ARRAY:
					if (iface.type(rhs) != LATTICE_TYPE_NUMBER) {
						set_error(errp, expr->item[1].expr->line,
							LATTICE_TYPE_ERROR, "index must be a number");

						iface.free(lhs); iface.free(rhs); iface.free(rhs2);
						return NULL;
					}

					if (fmod(iface.value(rhs).number, 1.0) != 0.0) {
						set_error(errp, expr->item[1].expr->line,
							LATTICE_VALUE_ERROR, "indices must be whole numbers");

						iface.free(lhs); iface.free(rhs); iface.free(rhs2);
						return NULL;
					}

					double rhs_number = iface.value(rhs).number;
					if (rhs_number < 0) rhs_number += iface.length(lhs);
					size_t i = (size_t) rhs_number;

					if (rhs2) {
						if (fmod(iface.value(rhs2).number, 1.0) != 0.0) {
							set_error(errp, expr->item[2].expr->line,
								LATTICE_VALUE_ERROR, "indices must be whole numbers");

							iface.free(lhs); iface.free(rhs); iface.free(rhs2);
							return NULL;
						}

						double rhs2_number = iface.value(rhs2).number;
						if (rhs2_number < 0) rhs2_number += iface.length(lhs);
						size_t j = (size_t) rhs2_number;

						if (i >= iface.length(lhs)) i = iface.length(lhs);
						if (j >= iface.length(lhs)) j = iface.length(lhs);
						if (j < i) j = i;

						if (iface.type(lhs) == LATTICE_TYPE_STRING) {
							const char *src = iface.value(lhs).string;
							char *string = calloc(j - i + 1, sizeof(char));
							for (size_t k = i; k < j; k++) string[k - i] = src[k];

							value = iface.create(
								LATTICE_TYPE_STRING,
								(lattice_value) { .string = string }
							);

							iface.free(lhs); iface.free(rhs); iface.free(rhs2);
							free(string);

							return value;
						} else {
							value = iface.create(LATTICE_TYPE_ARRAY, (lattice_value) {});
							for (size_t k = i; k < j; k++) {
								index.array = k;
								iface.add(value, NULL, iface.clone(iface.get(lhs, index)));
							}

							iface.free(lhs); iface.free(rhs); iface.free(rhs2);
							return value;
						}
					} else {
						if (i >= iface.length(lhs)) {
							set_error(errp, expr->item[1].expr->line,
								LATTICE_VALUE_ERROR, "index out of range");

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
							void *value = iface.clone(iface.get(lhs, index));

							iface.free(lhs); iface.free(rhs);
							return value;
						}
					}

				case LATTICE_TYPE_OBJECT:
					if (rhs2) {
						set_error(errp, expr->item[2].expr->line,
							LATTICE_TYPE_ERROR, "cannot range-index an object");

						iface.free(lhs); iface.free(rhs); iface.free(rhs2);
						return NULL;
					}

					if (iface.type(rhs) != LATTICE_TYPE_STRING) {
						set_error(errp, expr->item[1].expr->line,
							LATTICE_TYPE_ERROR, "index must be a string");

						iface.free(lhs); iface.free(rhs);
						return NULL;
					}

					index.object = iface.value(rhs).string;
					void *value = iface.clone(iface.get(lhs, index));
					iface.free(lhs); iface.free(rhs);

					if (!value) {
						set_error(errp, expr->item[1].expr->line,
							LATTICE_VALUE_ERROR, "index out of range");

						iface.free(value);
						return NULL;
					}

					return value;

				default:
					set_error(errp, expr->line,
						LATTICE_TYPE_ERROR, "can only index string, array, or object");

					iface.free(lhs); iface.free(rhs); iface.free(rhs2);
					return NULL;
			}

		case EXPR_TERNARY: {}
			void *cond_value = eval_expr(expr->item[0].expr, ctx, iface, errp);
			if (!cond_value) return NULL;
			bool cond = value_truthy(cond_value, iface);

			iface.free(cond_value);
			return eval_expr(expr->item[cond ? 1 : 2].expr, ctx, iface, errp);
	}
}

#undef LEX_ADD
#define LEX_ADD(ty) { \
	struct token *prev = tok; \
	tok = calloc(1, sizeof(struct token)); \
	tok->type = ty; \
	tok->prev = prev; \
	*tokp = tok; \
	tokp = &tok->next; \
}

#define LEX_ERR(msg) { \
	set_error(errp, line, LATTICE_SYNTAX_ERROR, msg); \
	free_token(tokf); \
	return NULL; \
}

static struct {
	const char *str;
	enum token_type type;
} keywords[] = {
	{ "if",      TOKEN_IF },
	{ "elif",    TOKEN_ELIF },
	{ "else",    TOKEN_ELSE },
	{ "switch",  TOKEN_SWITCH },
	{ "case",    TOKEN_CASE },
	{ "default", TOKEN_DEFAULT },
	{ "for",     TOKEN_FOR_ITER },
	{ "with",    TOKEN_WITH },
	{ "end",     TOKEN_END },
};

static struct token *lex(const char *src, lattice_error **errp) {
	const char *cspan = src;
	struct token *tok = NULL, *tokf = NULL, **tokp = &tokf;
	int line = 1;

	while (*src) {
		char c = *(src++);
		if (c == '$') {
			bool escape = *src == '$';
			if (escape) src++;

			if (src > cspan + 1) {
				LEX_ADD(TOKEN_SPAN);
				tok->ident = astrndup(cspan, src - cspan - 1);
			}

			if (!escape) {
				c = *(src++);
				switch (c) {
					case '(':
						do {
							src++;
							if (*src == '\n') line++;
						} while (*src && *src != ')');

						if (!*(src++)) LEX_ERR("unterminated comment");
						break;

					case '[':
					case '{': {}
						char sub_term[2] = { c + 2, 0 };
						struct expr_token *sub_tok = parse_expr(&src, sub_term, &line, errp);
						if (!sub_tok) {
							free_token(tokf);
							return NULL;
						}

						if (*(src++) != c + 2) {
							free_expr_token(sub_tok);
							LEX_ERR("expected closing bracket for substitution");
						}

						LEX_ADD(c == '[' ? TOKEN_SUB_ESC : TOKEN_SUB_RAW);
						tok->expr[0] = sub_tok;
						break;

					case '<': {}
						const char *path = src;

						do {
							src++;
							if (*src == '\n') line++;
						} while (*src && *src != '>');

						LEX_ADD(TOKEN_INCLUDE);
						tok->ident = astrndup(path, src - path);

						if (!*(src++)) LEX_ERR("unterminated include");
						break;

					default:
						src--;
						if (!*src) LEX_ERR("expected keyword");

						for (
							size_t i = sizeof(keywords) / sizeof(keywords[0]);
							i >= 0; i--
						) {
							if (i == 0) LEX_ERR("unknown keyword");

							size_t keyword_length = strlen(keywords[i - 1].str);
							if (strncmp(src, keywords[i - 1].str, keyword_length) == 0) {
								src += keyword_length;
								LEX_ADD(keywords[i - 1].type);
								break;
							}
						}

						if (tok->type != TOKEN_ELSE && tok->type != TOKEN_DEFAULT) {
							if (*src == '\n') line++;
							if (!isspace(*(src++))) LEX_ERR("expected whitespace");
						}

						switch (tok->type) {
							case TOKEN_IF:
							case TOKEN_ELIF:
							case TOKEN_SWITCH:
							case TOKEN_CASE:
							case TOKEN_WITH: {}
								struct expr_token *flow_tok = parse_expr(&src, ":", &line, errp);
								if (!flow_tok) {
									free_token(tokf);
									return NULL;
								}

								tok->expr[0] = flow_tok;
								break;

							case TOKEN_FOR_ITER:
								while (*src && isspace(*src)) {
									src++;
									if (*src == '\n') line++;
								}

								if (!*src || !(isalpha(*src) || *src == '_'))
									LEX_ERR("expected identifier for loop");

								const char *loop_ident = src;
								do src++; while (isalnum(*src) || *src == '_');

								tok->ident = astrndup(loop_ident, src - loop_ident);

								do {
									src++;
									if (*src == '\n') line++;
								} while (*src && isspace(*src));

								if (!*src) LEX_ERR("expected preposition for loop");

								if (strncmp(src, "from", 4) == 0)
									tok->type = TOKEN_FOR_RANGE_EXC;
								else if (strncmp(src, "in", 2) != 0)
									LEX_ERR("invalid loop preposition");

								src += tok->type == TOKEN_FOR_RANGE_EXC ? 4 : 2;

								while (*src && isspace(*src)) {
									src++;
									if (*src == '\n') line++;
								}

								if (tok->type == TOKEN_FOR_RANGE_EXC) {
									struct expr_token *low_tok = parse_expr(&src, "..", &line, errp);
									if (!low_tok) {
										free_token(tokf);
										return NULL;
									}

									tok->expr[0] = low_tok;

									if (strncmp(src, "..", 2) != 0) LEX_ERR("expected range");
									src += 2;

									if (*src == '=') {
										src++;
										tok->type = TOKEN_FOR_RANGE_INC;
									}

									struct expr_token *high_tok = parse_expr(&src, ":", &line, errp);
									if (!high_tok) {
										free_token(tokf);
										return NULL;
									}

									tok->expr[1] = high_tok;
								} else {
									struct expr_token *iter_tok = parse_expr(&src, ":", &line, errp);
									if (!iter_tok) {
										free_token(tokf);
										return NULL;
									}

									tok->expr[0] = iter_tok;
								}
								break;

							case TOKEN_ELSE:
							case TOKEN_DEFAULT:
							case TOKEN_END:
							default:
								break;
						}

						if (tok->type != TOKEN_END)
							if (*(src++) != ':') LEX_ERR("expected colon");
						break;
				}
			}

			cspan = src;
		} else if (c == '\n') {
			line++;
		}
	}

	if (src > cspan + 1) {
		LEX_ADD(TOKEN_SPAN);
		tok->ident = astrndup(cspan, src - cspan);
	}

	return tokf;
}

static void *parse_level(struct token **tokp, lattice_error **errp) {
	struct token *parent = (*tokp)->prev;
	if (parent) parent->child = *tokp;

	(*tokp)->prev = NULL;

	for (; *tokp; *tokp = (*tokp)->next) {
		(*tokp)->parent = parent;

		switch ((*tokp)->type) {
			case TOKEN_SPAN:
			case TOKEN_SUB_ESC:
			case TOKEN_SUB_RAW:
			case TOKEN_INCLUDE:
				break;

			case TOKEN_SWITCH: {}
				struct token *swtch = *tokp;
				swtch->child = (*tokp)->next;
				(*tokp)->next->prev = NULL;

				for (*tokp = (*tokp)->next; *tokp; *tokp = (*tokp)->next) {
					(*tokp)->parent = swtch;

					if ((*tokp)->type == TOKEN_CASE || (*tokp)->type == TOKEN_DEFAULT) {
						*tokp = (*tokp)->next;
						(*tokp)->prev->next = NULL;
						if (!parse_level(tokp, errp)) return NULL;
					}

					if ((*tokp)->type == TOKEN_END) {
						swtch->next = (*tokp)->next;
						if ((*tokp)->prev) (*tokp)->prev->next = NULL;
						if ((*tokp)->next) (*tokp)->next->prev = swtch;

						free(*tokp);
						*tokp = swtch;
						break;
					}
				}

				break;

			case TOKEN_CASE:
			case TOKEN_DEFAULT:
				if (parent->type != TOKEN_CASE && parent->type != TOKEN_DEFAULT) {
					set_error(errp, (*tokp)->line,
						LATTICE_SYNTAX_ERROR, "case outside of switch");

					return NULL;
				}

				parent->next = *tokp;
				if ((*tokp)->prev) (*tokp)->prev->next = NULL;
				(*tokp)->prev = parent;
				*tokp = parent;
				return (void *) 1;

			case TOKEN_END:
				if (!parent) {
					set_error(errp, (*tokp)->line,
						LATTICE_SYNTAX_ERROR, "unexpected block terminator");

					return NULL;
				}

				if (parent->type == TOKEN_CASE || parent->type == TOKEN_DEFAULT)
					return (void *) 1;

				parent->next = (*tokp)->next;
				if ((*tokp)->prev) (*tokp)->prev->next = NULL;
				if ((*tokp)->next) (*tokp)->next->prev = parent;

				free(*tokp);
				*tokp = parent;
				return (void *) 1;

			case TOKEN_ELIF:
			case TOKEN_ELSE:
				if ((*tokp)->prev->type != TOKEN_IF && (*tokp)->prev->type != TOKEN_ELIF) {
					parent->next = *tokp;
					if ((*tokp)->prev) (*tokp)->prev->next = NULL;
					(*tokp)->prev = parent;
					(*tokp)->parent = parent->parent;

					*tokp = parent;
					return (void *) 1;
				}
			default:
				*tokp = (*tokp)->next;
				if (!parse_level(tokp, errp)) return NULL;
				break;
		}
	}

	if (parent) {
		set_error(errp, 0, LATTICE_SYNTAX_ERROR, "unexpected end of file");

		return NULL;
	}

	return (void *) 1;
}

struct include_stack {
	const char *name;
	struct include_stack *below;
};

static struct token *parse(
	const char *src,
	lattice_opts opts,
	struct include_stack *stack,
	lattice_error **errp
);

static void *parse_resolve(
	struct token *tok,
	lattice_opts opts,
	struct include_stack *stack,
	lattice_error **errp
) {
	for (; tok; tok = tok->next) {
		if (tok->type == TOKEN_INCLUDE) {
			char *resolved = NULL, *src;

			if (!opts.search || !opts.resolve) {
				if (opts.resolve) {
					resolved = opts.resolve(tok->ident);
					if (!resolved) return NULL;
				} else {
					const char *search_cwd[] = { ".", NULL };
					const char * const *search = opts.search ? opts.search : search_cwd;

					for (; *search; search++) {
						DIR *dir = opendir(*search);
						if (!dir) continue;

						int dir_fd = dirfd(dir);
						if (dir_fd == -1) {
							closedir(dir);
							continue;
						}

						int fd = openat(dir_fd, tok->ident, O_RDONLY);
						if (fd == -1) {
							closedir(dir);
							continue;
						}

						resolved = malloc(strlen(*search) + strlen(tok->ident) + 2);
						strcpy(resolved, *search);
						strcat(resolved, "/");
						strcat(resolved, tok->ident);

						closedir(dir);
						close(fd);
						break;
					}

					if (!resolved) {
						set_error(errp, tok->line,
							LATTICE_INCLUDE_ERROR, "failed to resolve include");

						return NULL;
					}
				}

				for (struct include_stack *part = stack; part; part = part->below) {
					if (part->name && strcmp(part->name, resolved) == 0) {
						lattice_error *err = set_error(errp, tok->line,
							LATTICE_INCLUDE_ERROR, "");
						if (err) {
							free(err->message);
							err->message = format("recursive include of '%s'", resolved);
						}

						free(resolved);
						return NULL;
					}
				}
			}

			if (resolved) {
				struct stat statbuf;
				if (stat(resolved, &statbuf) == -1) {
					set_error(errp, tok->line,
						LATTICE_INCLUDE_ERROR, "failed to stat include");

					free(resolved);
					return NULL;
				}

				int fd = open(resolved, O_RDONLY);
				if (fd == -1) {
					set_error(errp, tok->line,
						LATTICE_INCLUDE_ERROR, "failed to open include");

					free(resolved);
					return NULL;
				}

				src = malloc(statbuf.st_size + 1);
				src[statbuf.st_size] = 0;

				if (read(fd, src, statbuf.st_size) == -1) {
					set_error(errp, tok->line,
						LATTICE_INCLUDE_ERROR, "failed to read include");

					free(resolved);
					free(src);
					return NULL;
				}

				close(fd);
			} else {
				src = opts.resolve(tok->ident);
				if (!src) {
					set_error(errp, tok->line,
						LATTICE_INCLUDE_ERROR, "failed to resolve include");

					return NULL;
				}
			}

			struct include_stack new_stack = {
				.name = resolved,
				.below = stack,
			};

			tok->child = parse(src, opts, resolved ? &new_stack : stack, errp);

			free(resolved);
			free(src);

			if (!tok->child) return NULL;
		} else {
			if (!parse_resolve(tok->child, opts, stack, errp)) return NULL;
		}
	}

	return (void *) 1;
}

static struct token *parse(
	const char *src,
	lattice_opts opts,
	struct include_stack *stack,
	lattice_error **errp
) {
	struct token *tok = lex(src, errp), *tokt = tok;
	if (!tok) return NULL;

	if (!parse_level(&tokt, errp)) return NULL;
	if (!parse_resolve(tok, opts, stack, errp)) return NULL;

	return tok;
}

#define EVAL_EMIT(str) { \
	if (str[0] != 0) { \
		resb = emit(str, emit_ctx); \
		if (!opts.ignore_emit_zero && resb == 0) { \
			set_error(errp, tok->line, \
				LATTICE_IO_ERROR, "failed to write output"); \
			return 0; \
		} else { \
			res += resb; \
		} \
	} \
}
#define EVAL_SUB(tok, ctx, ...) { \
	res += eval(tok, ctx, emit, emit_ctx, iface, opts, &err); \
	if (err) { \
		if (errp) *errp = err; \
		__VA_ARGS__ \
		return 0; \
	} \
}

size_t eval(
	struct token *tok,
	const void *ctx,
	lattice_emit emit,
	void *emit_ctx,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **errp
) {
	size_t res = 0, resb;
	bool was_if = false, end_if;
	void *value;
	lattice_error *err = NULL;

	for (; tok; tok = tok->next) {
		if (tok->type == TOKEN_ELIF || tok->type == TOKEN_ELSE) {
			if (!was_if) {
				set_error(errp, tok->line,
					LATTICE_SYNTAX_ERROR, "unexpected subclause");

				return 0;
			}

			if (end_if) continue;
		}

		switch (tok->type) {
			case TOKEN_SPAN:
				EVAL_EMIT(tok->ident);
				break;

			case TOKEN_SUB_ESC:
			case TOKEN_SUB_RAW:
				value = eval_expr(tok->expr[0], ctx, iface, errp);
				if (!value) return 0;

				char *str = iface.type(value) != LATTICE_TYPE_STRING
					? iface.print(value)
					: astrdup(iface.value(value).string);
				iface.free(value);

				if (!str) {
					set_error(errp, tok->line,
						LATTICE_JSON_ERROR, "failed to serialise substitution value");

					return 0;
				}

				if (tok->type == TOKEN_SUB_ESC) {
					char *str_new;
					if (opts.escape) {
						str_new = opts.escape(str);
					} else {
						int i, extd = 0;
						for (i = 0; str[i]; i++) {
							switch (str[i]) {
								case '&':
								case '\'':
								case '"':
								case '<':
								case '>':
									extd += 4;
									break;

								default:
									break;
							}
						}

						str_new = malloc(i + extd + 1);
						str_new[i + extd] = 0;

						for (int j = 0, k = 0; str[j]; j++, k++) {
							switch (str[j]) {
								case '&':
								case '\'':
								case '"':
								case '<':
								case '>':
									sprintf(str_new + k, "&#%02d;", str[j]);
									k += 4;
									break;

								default:
									str_new[k] = str[j];
									break;
							}
						}
					}

					free(str);
					str = str_new;
				}

				EVAL_EMIT(str);
				free(str);
				break;

			case TOKEN_INCLUDE:
				EVAL_SUB(tok->child, ctx, if (errp) (*errp)->file = astrdup(tok->ident););
				break;

			case TOKEN_IF:
				value = eval_expr(tok->expr[0], ctx, iface, errp);
				if (!value) return 0;

				end_if = value_truthy(value, iface);
				iface.free(value);

				if (end_if) EVAL_SUB(tok->child, ctx);
				break;

			case TOKEN_ELIF:
				value = eval_expr(tok->expr[0], ctx, iface, errp);
				if (!value) return 0;

				end_if = value_truthy(value, iface);
				iface.free(value);

				if (end_if) EVAL_SUB(tok->child, ctx);
				break;

			case TOKEN_ELSE:
				EVAL_SUB(tok->child, ctx);
				break;

			case TOKEN_SWITCH:
				value = eval_expr(tok->expr[0], ctx, iface, errp);
				if (!value) return 0;

				for (struct token *child = tok->child; child; child = child->next) {
					if (child->type == TOKEN_CASE) {
						void *branch = eval_expr(child->expr[0], ctx, iface, errp);
						bool eq = value_eq(value, branch, iface);
						iface.free(branch);

						if (eq) {
							EVAL_SUB(child->child, ctx, iface.free(value););
							break;
						}
					} else if (child->type == TOKEN_DEFAULT) {
						if (child->next) {
							set_error(errp, child->line,
								LATTICE_SYNTAX_ERROR, "cannot have case after default");

							iface.free(value);
							return 0;
						}

						EVAL_SUB(child->child, ctx, iface.free(value););
					}
				}

				iface.free(value);
				break;

			case TOKEN_FOR_RANGE_EXC:
			case TOKEN_FOR_RANGE_INC:
			case TOKEN_FOR_ITER: {}
				bool anon_for = strcmp(tok->ident, "_") == 0;

				if (iface.type(ctx) != LATTICE_TYPE_OBJECT && !anon_for) {
					set_error(errp, tok->line,
						LATTICE_TYPE_ERROR, "cannot bind in non-object scope");

					return 0;
				}

				void *from = eval_expr(tok->expr[0], ctx, iface, errp), *to = NULL;
				if (!from) return 0;

				if (tok->type == TOKEN_FOR_ITER) {
					switch (iface.type(from)) {
						case LATTICE_TYPE_STRING:
						case LATTICE_TYPE_ARRAY:
						case LATTICE_TYPE_OBJECT:
							break;

						default:
							set_error(errp, tok->line,
								LATTICE_TYPE_ERROR, "loop values must be iterable");

							iface.free(from);
							return 0;
					}
				} else {
					to = eval_expr(tok->expr[1], ctx, iface, errp);
					if (!to) {
						iface.free(from);
						return 0;
					}

					if (
						iface.type(from) != LATTICE_TYPE_NUMBER ||
						iface.type(to) != LATTICE_TYPE_NUMBER
					) {
						set_error(errp, tok->line,
							LATTICE_TYPE_ERROR, "loop indices must be numbers");

						iface.free(from);
						iface.free(to);
						return 0;
					}
				}

				size_t scope_length = iface.length(ctx), remove_index = scope_length;
				const char **scope_keys = malloc(sizeof(const char*) * scope_length);

				if (!anon_for) {
					iface.keys(ctx, scope_keys);

					for (size_t i = 0; i < scope_length; i++) {
						if (strcmp(scope_keys[i], tok->ident) == 0) {
							remove_index = i;
							break;
						}
					}
				}

				double from_num, to_num;
				size_t from_length;
				const char **from_keys = NULL;

				if (tok->type == TOKEN_FOR_ITER) {
					from_length = iface.length(from);

					if (iface.type(from) == LATTICE_TYPE_OBJECT) {
						from_keys = malloc(sizeof(const char *) * from_length);
						iface.keys(from, from_keys);
					}
				} else {
					from_num = iface.value(from).number;
					double to_num_off = tok->type == TOKEN_FOR_RANGE_INC ? 1 : 0;
					to_num = iface.value(to).number + to_num_off;
				}

				if (tok->type != TOKEN_FOR_ITER) iface.free(from);
				if (to) iface.free(to);

				for (size_t i = 0;; i++) {
					if (tok->type == TOKEN_FOR_ITER) {
						if (i >= from_length) break;
					} else {
						if (from_num >= to_num) break;
					}

					const void *scope = ctx;

					if (!anon_for) {
						scope = iface.create(LATTICE_TYPE_OBJECT, (lattice_value) {});
						for (size_t i = 0; i < scope_length; i++)
							if (i != remove_index)
								iface.add((void *) scope, scope_keys[i], iface.clone(
									iface.get(ctx, (lattice_index) { .object = scope_keys[i] })
								));

						void *bind;
						if (tok->type == TOKEN_FOR_ITER) {
							switch (iface.type(from)) {
								case LATTICE_TYPE_STRING: {}
									char slice[2] = { iface.value(from).string[i], 0 };
									bind = iface.create(
										LATTICE_TYPE_STRING,
										(lattice_value) { .string = slice }
									);
									break;

								case LATTICE_TYPE_ARRAY:
									bind = iface.clone(
										iface.get(from, (lattice_index) { .array = i })
									);
									break;

								case LATTICE_TYPE_OBJECT:
									bind = iface.create(
										LATTICE_TYPE_STRING,
										(lattice_value) { .string = from_keys[i] }
									);
									break;

								default: return 0;
							}
						} else {
							bind = iface.create(
								LATTICE_TYPE_NUMBER,
								(lattice_value) { .number = from_num }
							);
						}

						iface.add((void *) scope, tok->ident, bind);
					}

					res += eval(tok->child, scope, emit, emit_ctx, iface, opts, &err);

					if (!anon_for) iface.free((void *) scope);

					if (err) {
						if (errp) *errp = err;
						free(scope_keys);
						if (tok->type == TOKEN_FOR_ITER) iface.free(from);
						if (from_keys) free(from_keys);
						return 0;
					}

					if (tok->type != TOKEN_FOR_ITER) {
						from_num++;
					}
				}

				free(scope_keys);
				if (tok->type == TOKEN_FOR_ITER) iface.free(from);
				if (from_keys) free(from_keys);
				break;

			case TOKEN_WITH:
				value = eval_expr(tok->expr[0], ctx, iface, errp);
				if (!value) return 0;

				EVAL_SUB(tok->child, value);

				iface.free(value);
				break;

			case TOKEN_CASE:
			case TOKEN_DEFAULT:
			case TOKEN_END:
				break;
		}

		was_if = tok->type == TOKEN_IF || tok->type == TOKEN_ELIF;
	}

	return res;
}

size_t lattice(
	const char *template,
	const void *root,
	lattice_emit emit,
	void *emit_ctx,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **errp
) {
	struct token *tok = parse(template, opts, NULL, errp);
	if (!tok) return 0;

	size_t res = eval(tok, root, emit, emit_ctx, iface, opts, errp);

	free_token(tok);
	return res;
}

static size_t file_emit(const char *data, void *file) {
	return fputs(data, (FILE *) file) == EOF ? 0 : strlen(data);
}

size_t lattice_file(
	const char *template,
	const void *root,
	FILE *file,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **errp
) {
	return lattice(template, root, file_emit, file, iface, opts, errp);
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
	return ctx->length - offset;
}

size_t lattice_buffer(
	const char *template,
	const void *root,
	char **buffer,
	lattice_iface iface,
	lattice_opts opts,
	lattice_error **errp
) {
	*buffer = calloc(1, 1);
	struct buffer_ctx ctx = { .length = 0, .allocated = 1, .buffer = buffer };

	return lattice(template, root, buffer_emit, &ctx, iface, opts, errp);
}
