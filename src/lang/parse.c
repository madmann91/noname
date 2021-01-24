#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "lang/ast.h"
#include "utils/utf8.h"
#include "utils/lexer.h"
#include "utils/buf.h"
#include "utils/format.h"

#define MAX_AHEAD 3

#define SYMBOLS(f) \
    f(LPAREN, "(") \
    f(RPAREN, ")") \
    f(LBRACE, "{") \
    f(RBRACE, "}") \
    f(LBRACKET, "[") \
    f(RBRACKET, "]") \
    f(LANGLE, "<") \
    f(RANGLE, ">") \
    f(THINARROW, "->") \
    f(FATARROW, "=>") \
    f(DOT, ".") \
    f(COLON, ":") \
    f(SEMICOLON, ";") \
    f(COMMA, ",") \
    f(PLUS, "+") \
    f(MINUS, "-") \
    f(STAR, "*") \
    f(VBAR, "|") \
    f(BACKSLASH, "\\") \
    f(EQ, "=")

#define KEYWORDS(f) \
    f(UNIVERSE, "Universe") \
    f(TYPE, "Type") \
    f(UINT, "UInt") \
    f(NAT, "Nat") \
    f(INT, "Int") \
    f(FLOAT, "Float") \
    f(IN, "in") \
    f(LET, "let") \
    f(LETREC, "letrec") \
    f(CASE, "case") \
    f(OF, "of")

#define SPECIAL(f) \
    f(IDENT, "identifier") \
    f(LIT, "literal") \
    f(ERR, "error") \
    f(EOF, "end-of-file")

#define TOKENS(f) \
    SYMBOLS(f) \
    KEYWORDS(f) \
    SPECIAL(f) \

#define COPY_STR(str, begin, end) \
    size_t str##_len = (end) - (begin); \
    char* str = new_buf(char, str##_len + 1); \
    memcpy(str, (begin), str##_len); str[str##_len] = 0;

struct tok {
    enum {
#define f(x, str) TOK_##x,
        TOKENS(f)
#undef f
    } tag;
    struct lit lit;
    struct loc loc;
};

struct parser {
    struct arena** arena;
    struct lexer lexer;
    struct pos prev_end;
    struct tok ahead[MAX_AHEAD];
};

static inline const char* tok_quote(unsigned tok) {
    static const char* quotes[] = {
#define f(name, str) "'",
        SYMBOLS(f)
        KEYWORDS(f)
#undef f
#define f(name, str) "",
        SPECIAL(f)
#undef f
    };
    return quotes[tok];
}

static inline unsigned tok_style(unsigned tok) {
    static const unsigned styles[] = {
#define f(name, str) 0,
        SYMBOLS(f)
#undef f
#define f(name, str) STYLE_KEYWORD,
        KEYWORDS(f)
#undef f
#define f(name, str) 0,
        SPECIAL(f)
#undef f
    };
    return styles[tok];
}

static inline const char* tok_name(unsigned tok) {
    static const char* names[] = {
#define f(name, str) str,
        TOKENS(f)
#undef f
    };
    return names[tok];
}

static inline struct tok make_tok(const struct lexer* lexer, const struct pos* begin, unsigned tag) {
    return (struct tok) {
        .tag = tag,
        .loc = (struct loc) {
            .file  = lexer->file,
            .begin = *begin,
            .end   = lexer->pos
        }
    };
}

// Lexer ---------------------------------------------------------------------------

static inline struct tok lex(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);

        struct pos begin = lexer->pos;
        if (lexer->pos.ptr == lexer->end)
            return make_tok(lexer, &begin, TOK_EOF);

        // Symbols
        if (accept_char(lexer, '('))  return make_tok(lexer, &begin, TOK_LPAREN);
        if (accept_char(lexer, ')'))  return make_tok(lexer, &begin, TOK_RPAREN);
        if (accept_char(lexer, '{'))  return make_tok(lexer, &begin, TOK_LBRACE);
        if (accept_char(lexer, '}'))  return make_tok(lexer, &begin, TOK_RBRACE);
        if (accept_char(lexer, '['))  return make_tok(lexer, &begin, TOK_LBRACKET);
        if (accept_char(lexer, ']'))  return make_tok(lexer, &begin, TOK_RBRACKET);
        if (accept_char(lexer, '.'))  return make_tok(lexer, &begin, TOK_DOT);
        if (accept_char(lexer, ':'))  return make_tok(lexer, &begin, TOK_COLON);
        if (accept_char(lexer, ';'))  return make_tok(lexer, &begin, TOK_SEMICOLON);
        if (accept_char(lexer, ','))  return make_tok(lexer, &begin, TOK_COMMA);
        if (accept_char(lexer, '\\')) return make_tok(lexer, &begin, TOK_BACKSLASH);
        if (accept_char(lexer, '|'))  return make_tok(lexer, &begin, TOK_VBAR);

        if (accept_char(lexer, '-')) {
            if (accept_char(lexer, '>'))
                return make_tok(lexer, &begin, TOK_THINARROW);
            return make_tok(lexer, &begin, TOK_MINUS);
        }

        if (accept_char(lexer, '=')) {
            if (accept_char(lexer, '>'))
                return make_tok(lexer, &begin, TOK_FATARROW);
            return make_tok(lexer, &begin, TOK_EQ);
        }

        // Keywords and identifiers
        if (*lexer->pos.ptr == '_' || isalpha(*lexer->pos.ptr)) {
            do {
                eat_char(lexer);
            } while (*lexer->pos.ptr == '_' || isalnum(*lexer->pos.ptr));
            unsigned* keyword = find_in_keywords(&lexer->keywords, (struct keyword) { begin.ptr, lexer->pos.ptr });
            return keyword ? make_tok(lexer, &begin, *keyword) : make_tok(lexer, &begin, TOK_IDENT);
        }

        // Comments and slash
        if (accept_char(lexer, '#')) {
            while (lexer->pos.ptr != lexer->end && *lexer->pos.ptr != '\n')
                eat_char(lexer);
            continue;
        }

        // Literals
        if (isdigit(*lexer->pos.ptr)) {
            bool dot = false, exp = false;
            int base = 10;
            if (accept_str(lexer, "0b") || accept_str(lexer, "0B")) {
                // Binary integer literal
                base = 2;
                while (lexer->pos.ptr != lexer->end && (*lexer->pos.ptr == '0' || *lexer->pos.ptr == '1'))
                    eat_char(lexer);
            } else if (accept_str(lexer, "0x") || accept_str(lexer, "0X")) {
                // Hexadecimal integer or floating-point literal
                base = 16;
                while (lexer->pos.ptr != lexer->end && isxdigit(*lexer->pos.ptr))
                    eat_char(lexer);
                if ((dot = accept_char(lexer, '.'))) {
                    while (lexer->pos.ptr != lexer->end && isxdigit(*lexer->pos.ptr))
                        eat_char(lexer);
                }
                exp = accept_char(lexer, 'p') || accept_char(lexer, 'P');
            } else if (accept_char(lexer, '0')) {
                // Octal integer literal
                base = 8;
                while (lexer->pos.ptr != lexer->end && *lexer->pos.ptr >= '0' && *lexer->pos.ptr <= '7')
                    eat_char(lexer);
            } else {
                // Regular (base 10) integer or floating-point literal
                while (lexer->pos.ptr != lexer->end && isdigit(*lexer->pos.ptr))
                    eat_char(lexer);
                if ((dot = accept_char(lexer, '.'))) {
                    while (lexer->pos.ptr != lexer->end && isdigit(*lexer->pos.ptr))
                        eat_char(lexer);
                }
                exp = accept_char(lexer, 'e') || accept_char(lexer, 'E');
            }
            if (exp) {
                (void)(accept_char(lexer, '+') || accept_char(lexer, '-'));
                while (lexer->pos.ptr != lexer->end && isdigit(*lexer->pos.ptr))
                    eat_char(lexer);
            }
            struct tok tok = make_tok(lexer, &begin, TOK_LIT);
            COPY_STR(str, begin.ptr, lexer->pos.ptr)
            if (exp || dot)
                tok.lit.float_val = strtod(str, NULL);
            else
                tok.lit.int_val = strtoumax(str, NULL, base);
            bool ok = errno == 0;
            free_buf(str);
            if (!ok) goto error;
            return tok;
        }

        eat_char(lexer);
error:
        {
            struct tok tok = make_tok(lexer, &begin, TOK_ERR);
            COPY_STR(str, begin.ptr, lexer->pos.ptr)
            log_error(lexer->log, &tok.loc, "invalid token '%0:s'", FORMAT_ARGS({ .s = str }));
            free_buf(str);
            return tok;
        }
    }
}

// Parsing helpers -----------------------------------------------------------------

static inline struct ast* make_ast(struct parser* parser, struct pos* begin, const struct ast* ast) {
    struct ast* copy = alloc_from_arena(parser->arena, sizeof(struct ast));
    memcpy(copy, ast, sizeof(struct ast));
    copy->loc.file = parser->lexer.file;
    copy->loc.begin = *begin;
    copy->loc.end = parser->prev_end;
    return copy;
}

static inline struct ast** append_ast(struct ast** asts, struct ast* next) {
    *asts = next;
    return &next->next;
}

static inline void eat_tok(struct parser* parser, unsigned tag) {
    assert(parser->ahead->tag == tag);
    (void)tag;
    parser->prev_end = parser->ahead->loc.end;
    for (int i = 1; i < MAX_AHEAD; ++i)
        parser->ahead[i - 1] = parser->ahead[i];
    parser->ahead[MAX_AHEAD - 1] = lex(&parser->lexer);
}

static inline bool accept_tok(struct parser* parser, unsigned tag) {
    if (parser->ahead->tag == tag) {
        eat_tok(parser, tag);
        return true;
    }
    return false;
}

static inline void expect_tok(struct parser* parser, unsigned tag) {
    if (accept_tok(parser, tag))
        return;
    COPY_STR(str, parser->ahead->loc.begin.ptr, parser->ahead->loc.end.ptr)
    log_error(
        parser->lexer.log, &parser->ahead->loc,
        "expected %0:$%1:s%2:s%1:s%3:$, but got '%4:s'",
        FORMAT_ARGS(
            { .style = tok_style(tag) },
            { .s     = tok_quote(tag) },
            { .s     = tok_name(tag)  },
            { .style = 0 },
            { .s     = str }));
    free_buf(str);
    eat_tok(parser, parser->ahead->tag);
}

// Parsing functions ---------------------------------------------------------------

static struct ast* parse_err(struct parser* parser, const char* msg) {
    struct pos begin = parser->ahead->loc.begin;
    COPY_STR(str, parser->ahead->loc.begin.ptr, parser->ahead->loc.end.ptr)
    log_error(
        parser->lexer.log, &parser->ahead->loc,
        "expected %0:s, but got '%1:$%2:s%3:$'",
        FORMAT_ARGS(
            { .s = msg },
            { .style = tok_style(parser->ahead->tag) },
            { .s = str },
            { .style = 0 }));
    free_buf(str);
    eat_tok(parser, parser->ahead->tag);
    return make_ast(parser, &begin, &(struct ast) { .tag = AST_ERR });
}

static struct ast* parse_ident(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    size_t len = parser->ahead->loc.end.ptr - parser->ahead->loc.begin.ptr;
    char* name = alloc_from_arena(parser->arena, len + 1);
    memcpy(name, parser->ahead->loc.begin.ptr, len);
    name[len] = 0;
    expect_tok(parser, TOK_IDENT);
    return make_ast(parser, &begin, &(struct ast) { .tag = AST_IDENT, .ident.name = name });
}

static struct ast* parse_lit(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    struct lit lit = parser->ahead->lit;
    eat_tok(parser, TOK_LIT);
    return make_ast(parser, &begin, &(struct ast) { .tag = AST_LIT, .lit = lit });
}

static struct ast* parse_exp(struct parser*);
static struct ast* parse_pat(struct parser*);

static struct ast* parse_paren(struct parser* parser, struct ast* (*parse_contents)(struct parser*)) {
    eat_tok(parser, TOK_LPAREN);
    struct ast* ast = parse_contents(parser);
    expect_tok(parser, TOK_RPAREN);
    return ast;
}

static struct ast* parse_let_or_letrec(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    bool is_rec = parser->ahead->tag == TOK_LETREC;
    eat_tok(parser, is_rec ? TOK_LETREC : TOK_LET);
    struct ast* names = NULL, **next_name = &names;
    struct ast* vals = NULL, **next_val = &vals;
    while (parser->ahead->tag == TOK_IDENT) {
        next_name = append_ast(next_name, parse_ident(parser));
        expect_tok(parser, TOK_EQ);
        next_val = append_ast(next_val, parse_exp(parser));
        if (!accept_tok(parser, TOK_COMMA))
            break;
    }
    expect_tok(parser, TOK_IN);
    struct ast* body = parse_exp(parser);
    return make_ast(parser, &begin, &(struct ast) {
        .tag = is_rec ? AST_LETREC : AST_LET,
        .let = {
            .names = names,
            .vals = vals,
            .body = body
        }
    });
}

static struct ast* parse_abs(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    eat_tok(parser, TOK_BACKSLASH);
    struct ast* param = parse_pat(parser);
    expect_tok(parser, TOK_THINARROW);
    struct ast* body = parse_exp(parser);
    return make_ast(parser, &begin, &(struct ast) {
        .tag = AST_ABS,
        .abs = {
            .param = param,
            .body = body
        }
    });
}

static struct ast* parse_case(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    eat_tok(parser, TOK_CASE);
    struct ast* arg = parse_exp(parser);
    expect_tok(parser, TOK_OF);
    accept_tok(parser, TOK_VBAR);
    struct ast* pats = NULL, **next_pat = &pats;
    struct ast* vals = NULL, **next_val = &vals;
    while (true) {
        struct ast* pat = parse_pat(parser);
        expect_tok(parser, TOK_FATARROW);
        struct ast* val = parse_exp(parser);
        next_pat = append_ast(next_pat, pat);
        next_val = append_ast(next_val, val);
        if (!accept_tok(parser, TOK_VBAR))
            break;
    }
    return make_ast(parser, &begin, &(struct ast) {
        .tag = AST_MATCH,
        .match = {
            .arg = arg,
            .pats = pats,
            .vals = vals
        }
    });
}

static inline struct ast* parse_prod_or_record(struct parser* parser, unsigned sep, struct ast* (*parse_arg)(struct parser*)) {
    struct pos begin = parser->ahead->loc.begin;
    eat_tok(parser, TOK_LBRACE);
    struct ast* args = NULL, **next_arg = &args;
    struct ast* fields = NULL, **next_field = &fields;
    while (parser->ahead->tag == TOK_IDENT) {
        next_field = append_ast(next_field, parse_ident(parser));
        expect_tok(parser, sep);
        next_arg = append_ast(next_arg, parse_arg(parser));
        if (!accept_tok(parser, TOK_COMMA))
            break;
    }
    expect_tok(parser, TOK_RBRACE);
    return make_ast(parser, &begin, &(struct ast) {
        .tag = sep == TOK_COLON ? AST_PROD : AST_RECORD,
        .record = {
            .fields = fields,
            .args = args
        }
    });
}

static struct ast* parse_pat(struct parser* parser) {
    switch (parser->ahead->tag) {
        case TOK_IDENT: {
            struct ast* ast = parse_ident(parser);
            if (accept_tok(parser, TOK_COLON)) {
                struct ast* type = parse_exp(parser);
                return make_ast(parser, &ast->loc.begin, &(struct ast) {
                    .tag = AST_ANNOT,
                    .annot = { .ast = ast, .type = type }
                });
            }
            return ast;
        }
        case TOK_LIT:
            return parse_lit(parser);
        case TOK_LPAREN:
            return parse_paren(parser, parse_pat);
        case TOK_LBRACE:
            return parse_prod_or_record(parser, TOK_EQ, parse_pat);
        default:
            return parse_err(parser, "pattern");
    }
}

static struct ast* parse_basic_exp(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    switch (parser->ahead->tag) {
        case TOK_IDENT: return parse_ident(parser);
        case TOK_LIT:   return parse_lit(parser);
        case TOK_NAT:
        case TOK_INT:
        case TOK_FLOAT: {
            unsigned tag =
                parser->ahead->tag == TOK_INT ? AST_INT :
                parser->ahead->tag == TOK_FLOAT ? AST_FLOAT : AST_NAT;
            eat_tok(parser, parser->ahead->tag);
            return make_ast(parser, &begin, &(struct ast) { .tag = tag });
        }
        case TOK_LPAREN: {
            eat_tok(parser, TOK_LPAREN);
            struct ast* ast = parse_exp(parser);
            expect_tok(parser, TOK_RPAREN);
            return ast;
        }
        case TOK_LBRACE:
            return parse_prod_or_record(parser, parser->ahead[2].tag, parse_exp);
        case TOK_LET:
        case TOK_LETREC:
            return parse_let_or_letrec(parser);
        case TOK_CASE:
            return parse_case(parser);
        case TOK_BACKSLASH:
            return parse_abs(parser);
        default:
            return parse_err(parser, "expression");
    }
}

static struct ast* parse_suffix_exp(struct parser* parser, struct ast* ast) {
    struct pos begin = ast->loc.begin;
    switch (parser->ahead->tag) {
        case TOK_THINARROW: {
            eat_tok(parser, TOK_THINARROW);
            struct ast* codom = parse_exp(parser);
            return make_ast(parser, &begin, &(struct ast) {
                .tag = AST_ARROW,
                .arrow = {
                    .dom = ast,
                    .codom = codom
                }
            });
        }
        case TOK_DOT: {
            eat_tok(parser, TOK_DOT);
            if (parser->ahead->tag == TOK_LBRACE) {
                struct ast* record = parse_prod_or_record(parser, TOK_EQ, parse_exp);
                return make_ast(parser, &begin, &(struct ast) {
                    .tag = AST_INS,
                    .ins = {
                        .val = ast,
                        .record = record
                    }
                });
            } else {
                struct ast* elem = parse_ident(parser);
                return make_ast(parser, &begin, &(struct ast) {
                    .tag = AST_EXT,
                    .ext = {
                        .val = ast,
                        .elem = elem
                    }
                });
            }
        }
        case TOK_IDENT:
        case TOK_LIT:
        case TOK_NAT:
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_LPAREN:
        case TOK_LBRACE:
        case TOK_BACKSLASH:
        case TOK_CASE:
        case TOK_LET:
        case TOK_LETREC: {
            struct ast* right = parse_basic_exp(parser);
            return make_ast(parser, &begin, &(struct ast) {
                .tag = AST_APP,
                .app = { .left = ast, .right = right }
            });
        }
        default:
            return ast;
    }
}

static struct ast* parse_exp(struct parser* parser) {
    struct ast* cur = parse_basic_exp(parser), *old;
    do {
        old = cur;
        cur = parse_suffix_exp(parser, old);
    } while (cur != old);
    return cur;
}
    
struct ast* parse_ast(struct arena** arena, struct log* log, const char* file_name, const char* data, size_t data_size) {
    struct parser parser = {
        .arena = arena,
        .lexer.file = file_name,
        .lexer.log = log,
        .lexer.pos = { .ptr = data, .row = 1, .col = 1 },
        .lexer.end = data + data_size,
        .prev_end  = { .row = 1, .col = 1 }
    };
    enum {
#define f(x, str) KEYWORD_##x,
        KEYWORDS(f)
#undef f
        KEYWORD_COUNT
    };
    parser.lexer.keywords = new_keywords_with_cap(KEYWORD_COUNT);
#define f(x, str) insert_in_keywords(&parser.lexer.keywords, (struct keyword) { .begin = str, .end = str + strlen(str) }, TOK_##x);
    KEYWORDS(f)
#undef f
    for (int i = 0; i < MAX_AHEAD; ++i)
        parser.ahead[i] = lex(&parser.lexer);
    struct ast* ast = parse_exp(&parser);
    free_keywords(&parser.lexer.keywords);
    return ast;
}
