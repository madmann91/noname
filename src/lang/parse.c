#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "lang/ast.h"
#include "utils/utf8.h"
#include "utils/lexer.h"
#include "utils/buf.h"
#include "utils/format.h"

#define SYMBOLS(f) \
    f(LPAREN, "(") \
    f(RPAREN, ")") \
    f(LBRACE, "{") \
    f(RBRACE, "}") \
    f(COLON, ":") \
    f(SEMICOLON, ";") \
    f(COMMA, ",") \
    f(PLUS, "+") \
    f(MINUS, "-") \
    f(STAR, "*") \
    f(SLASH, "/") \
    f(EQ, "=")

#define KEYWORDS(f) \
    f(UINT, "uint") \
    f(INT, "int") \
    f(FLOAT, "float") \
    f(FUN, "fun") \
    f(VAR, "var") \
    f(VAL, "val") \
    f(MOD, "mod")

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
    struct tok ahead;
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
        if (accept_char(lexer, '(')) return make_tok(lexer, &begin, TOK_LPAREN);
        if (accept_char(lexer, ')')) return make_tok(lexer, &begin, TOK_RPAREN);
        if (accept_char(lexer, '{')) return make_tok(lexer, &begin, TOK_LBRACE);
        if (accept_char(lexer, '}')) return make_tok(lexer, &begin, TOK_RBRACE);
        if (accept_char(lexer, ':')) return make_tok(lexer, &begin, TOK_COLON);
        if (accept_char(lexer, ';')) return make_tok(lexer, &begin, TOK_SEMICOLON);
        if (accept_char(lexer, ',')) return make_tok(lexer, &begin, TOK_COMMA);
        if (accept_char(lexer, '=')) return make_tok(lexer, &begin, TOK_EQ);

        // Keywords and identifiers
        if (*lexer->pos.ptr == '_' || isalpha(*lexer->pos.ptr)) {
            do {
                eat_char(lexer);
            } while (*lexer->pos.ptr == '_' || isalnum(*lexer->pos.ptr));
            unsigned* keyword = find_in_keywords(&lexer->keywords, (struct keyword) { begin.ptr, lexer->pos.ptr });
            return keyword ? make_tok(lexer, &begin, *keyword) : make_tok(lexer, &begin, TOK_IDENT);
        }

        // Comments and slash
        if (accept_char(lexer, '/')) {
            if (accept_char(lexer, '/')) {
                while (lexer->pos.ptr != lexer->end && *lexer->pos.ptr != '\n')
                    eat_char(lexer);
                continue;
            } else if (accept_char(lexer, '*')) {
                while (true) {
                    while (lexer->pos.ptr != lexer->end && *lexer->pos.ptr != '*')
                        eat_char(lexer);
                    if (lexer->pos.ptr == lexer->end) {
                        struct loc loc = { .file = lexer->file, .begin = begin, .end = lexer->pos };
                        log_error(lexer->log, &loc, "unterminated multiline comment", NULL);
                        break;
                    }
                    eat_char(lexer);
                    if (accept_char(lexer, '/'))
                        break;
                }
                continue;
            }
            return make_tok(lexer, &begin, TOK_SLASH);
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
            log_error(lexer->log, &tok.loc, "invalid token '%0:s'", FMT_ARGS({ .s = str }));
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
    assert(parser->ahead.tag == tag);
    (void)tag;
    parser->prev_end = parser->ahead.loc.end;
    parser->ahead = lex(&parser->lexer);
}

static inline bool accept_tok(struct parser* parser, unsigned tag) {
    if (parser->ahead.tag == tag) {
        eat_tok(parser, tag);
        return true;
    }
    return false;
}

static inline void expect_tok(struct parser* parser, unsigned tag) {
    if (accept_tok(parser, tag))
        return;
    COPY_STR(str, parser->ahead.loc.begin.ptr, parser->ahead.loc.end.ptr)
    log_error(
        parser->lexer.log, &parser->ahead.loc,
        "expected %0:$%1:s%2:s%1:s%3:$, but got '%4:s'",
        FMT_ARGS(
            { .style = tok_style(tag) },
            { .s     = tok_quote(tag) },
            { .s     = tok_name(tag)  },
            { .style = 0 },
            { .s     = str }));
    free_buf(str);
    eat_tok(parser, parser->ahead.tag);
}

// Parsing functions ---------------------------------------------------------------

static struct ast* parse_err(struct parser* parser, const char* msg) {
    struct pos begin = parser->ahead.loc.begin;
    COPY_STR(str, parser->ahead.loc.begin.ptr, parser->ahead.loc.end.ptr)
    log_error(
        parser->lexer.log, &parser->ahead.loc,
        "expected %0:s, but got '%1:$%2:s%3:$'",
        FMT_ARGS(
            { .s = msg },
            { .style = tok_style(parser->ahead.tag) },
            { .s = str },
            { .style = 0 }));
    free_buf(str);
    eat_tok(parser, parser->ahead.tag);
    return make_ast(parser, &begin, &(struct ast) { .tag = AST_ERR });
}

static struct ast* parse_ident(struct parser* parser) {
    struct pos begin = parser->ahead.loc.begin;
    size_t len = parser->ahead.loc.end.ptr - parser->ahead.loc.begin.ptr;
    char* str = alloc_from_arena(parser->arena, len + 1);
    memcpy(str, parser->ahead.loc.begin.ptr, len);
    str[len] = 0;
    expect_tok(parser, TOK_IDENT);
    return make_ast(parser, &begin, &(struct ast) { .tag = AST_IDENT, .ident.str = str });
}

static bool is_exp_ahead(struct parser* parser) {
    return
        parser->ahead.tag == TOK_IDENT ||
        parser->ahead.tag == TOK_LPAREN ||
        parser->ahead.tag == TOK_LIT ||
        parser->ahead.tag == TOK_INT ||
        parser->ahead.tag == TOK_FLOAT;
}

static struct ast* parse_app(struct parser*);
static struct ast* parse_exp_or_pat(struct parser*, bool);
static struct ast* parse_exp(struct parser* parser) { return parse_exp_or_pat(parser, false); }
static struct ast* parse_pat(struct parser* parser) { return parse_exp_or_pat(parser, true); }

static struct ast* parse_exp_or_pat(struct parser* parser, bool is_pat) {
    struct pos begin = parser->ahead.loc.begin;
    switch (parser->ahead.tag) {
        case TOK_IDENT: {
            struct ast* ast = parse_ident(parser);
            if (accept_tok(parser, TOK_COLON)) {
                struct ast* type = parse_app(parser);
                return make_ast(parser, &begin, &(struct ast) {
                    .tag = AST_ANNOT,
                    .annot = {
                        .ast = ast,
                        .type = type
                    }
                });
            }
            return ast;
        }
        case TOK_LIT: {
            struct lit lit = parser->ahead.lit;
            eat_tok(parser, TOK_LIT);
            return make_ast(parser, &begin, &(struct ast) { .tag = AST_LIT, .lit = lit });
        }
        case TOK_INT:
        case TOK_FLOAT: {
            if (is_pat) goto error;
            unsigned tag = parser->ahead.tag == TOK_INT ? AST_INT : AST_FLOAT;
            eat_tok(parser, parser->ahead.tag);
            return make_ast(parser, &begin, &(struct ast) { .tag = tag });
        }
        case TOK_LPAREN: {
            struct ast* args = NULL, **next = &args;
            eat_tok(parser, TOK_LPAREN);
            while (is_exp_ahead(parser)) {
                next = append_ast(next, is_pat ? parse_pat(parser) : parse_app(parser));
                if (!accept_tok(parser, TOK_COMMA))
                    break;
            }
            expect_tok(parser, TOK_RPAREN);
            return make_ast(parser, &begin, &(struct ast) {
                .tag = AST_TUP,
                .tup.args = args
            });
        }
        default:
            break;
    }
error:
    return parse_err(parser, is_pat ? "pattern" : "expression");
}

static struct ast* parse_app(struct parser* parser) {
    struct pos begin = parser->ahead.loc.begin;
    struct ast* left = parse_exp(parser);
    while (is_exp_ahead(parser)) {
        struct ast* right = parse_exp(parser);
        left = make_ast(parser, &begin, &(struct ast) {
            .tag = AST_APP,
            .app = { .left = left, .right = right }
        });
    }
    return left;
}
    
static struct ast* parse_fun(struct parser* parser) {
    struct pos begin = parser->ahead.loc.begin;
    eat_tok(parser, TOK_FUN);
    struct ast* name = parse_ident(parser);
    struct ast* param = parse_pat(parser);
    struct ast* ret_type = NULL;
    if (accept_tok(parser, TOK_COLON))
        ret_type = parse_app(parser);
    expect_tok(parser, TOK_EQ);
    struct ast* body = parse_app(parser);
    return make_ast(parser, &begin, &(struct ast) {
        .tag = AST_FUN,
        .fun = {
            .name = name,
            .param = param,
            .ret_type = ret_type,
            .body = body
        }
    });
}

static struct ast* parse_decl(struct parser* parser) {
    switch (parser->ahead.tag) {
        case TOK_FUN:
            return parse_fun(parser);
        default:
            return parse_err(parser, "declaration");
    }
}

static struct ast* parse_prog(struct parser* parser) {
    struct pos begin = parser->ahead.loc.begin;
    struct ast* decls = NULL, **next = &decls; 
    while (parser->ahead.tag != TOK_EOF)
        next = append_ast(next, parse_decl(parser));
    return make_ast(parser, &begin, &(struct ast) { .tag = AST_MOD, .mod.decls = decls });
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
    parser.ahead = lex(&parser.lexer);
    struct ast* ast = parse_prog(&parser);
    free_keywords(&parser.lexer.keywords);
    return ast;
}
