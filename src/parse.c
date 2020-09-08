#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include "parse.h"
#include "utils.h"
#include "utf8.h"
#include "log.h"
#include "vec.h"

#define TOKENS(f) \
    f(LPAREN, "(") \
    f(RPAREN, ")") \
    f(ABS, "abs") \
    f(APP, "app") \
    f(BOT, "bot") \
    f(FVAR, "fvar") \
    f(INT, "int") \
    f(INJ, "inj") \
    f(LET, "let") \
    f(MATCH, "match") \
    f(PI, "pi") \
    f(PROD, "prod") \
    f(REAL, "real") \
    f(STAR, "star") \
    f(SUM, "sum") \
    f(TOP, "top") \
    f(TUP, "tup") \
    f(UNI, "uni") \
    f(INT_LIT, "integer literal") \
    f(REAL_LIT, "floating-point literal") \
    f(WILD, "_") \
    f(DEBRUIJN, "de-bruijn index") \
    f(ID, "identifier") \
    f(ERR, "error") \
    f(EOF, "end-of-file")

struct tok {
    enum {
#define TOK(name, str) TOK_##name,
        TOKENS(TOK)
#undef TOK
    } tag;
    union {
        struct {
            size_t index;
            size_t sub_index;
        } debruijn;
        union lit lit;
    };
    const char* begin, *end;
    struct loc loc;
};

struct lexer {
    const char* cur;
    const char* end;
    const char* file;
    struct log* log;
    int row, col;
};

struct env {
    exp_t*  exps;
    size_t* levels;
};

struct parser {
    mod_t mod;
    struct lexer lexer;
    struct tok ahead;
    struct loc prev_loc;
    exp_t uni, star;
    struct env env;
};

static inline const char* tok_to_str(unsigned tok) {
    switch (tok) {
#define TOK(name, str) case TOK_##name: return str;
        TOKENS(TOK)
#undef TOK
        default:
            assert(false);
            return "";
    }
}

static struct tok lex(struct lexer* lexer);

parser_t new_parser(mod_t mod, log_t log, const char* file, const char* begin, size_t size) {
    parser_t parser = xmalloc(sizeof(struct parser));
    parser->mod = mod;
    parser->lexer.file = file;
    parser->lexer.log = log;
    parser->lexer.cur = begin;
    parser->lexer.end = begin + size;
    parser->lexer.row = 1;
    parser->lexer.col = 1;
    parser->prev_loc = (struct loc) { .end = { .row = 1, .col = 1 } };
    parser->uni = parser->star = NULL;
    parser->env.exps   = NEW_VEC(exp_t);
    parser->env.levels = NEW_VEC(size_t);
    parser->ahead = lex(&parser->lexer);
    return parser;
}

void free_parser(parser_t parser) {
    FREE_VEC(parser->env.exps);
    FREE_VEC(parser->env.levels);
    free(parser);
}

static inline void eat_char(struct lexer* lexer) {
    assert(lexer->cur < lexer->end);
    if (is_utf8_multibyte(*lexer->cur)) {
        size_t n = eat_utf8_bytes(lexer->cur);
        if (lexer->cur + n > lexer->end)
            n = lexer->end - lexer->cur;
        lexer->cur += n;
        lexer->col++;
    } else {
        if (*lexer->cur == '\n')
            ++lexer->row, lexer->col = 1;
        else
            lexer->col++;
        lexer->cur++;
    }
}

static inline void eat_spaces(struct lexer* lexer) {
    while (lexer->cur != lexer->end && isspace(*lexer->cur))
        eat_char(lexer);
}

static inline bool accept_char(struct lexer* lexer, char c) {
    if (*lexer->cur == c) {
        eat_char(lexer);
        return true;
    }
    return false;
}

static inline bool accept_str(struct lexer* lexer, const char* str) {
    size_t len = strlen(str);
    if (lexer->cur + len > lexer->end)
        return false;
    for (size_t i = 0; i < len; ++i) {
        if (str[i] != lexer->cur[i])
            return false;
    }
    const char* begin = lexer->cur;
    while (lexer->cur < begin + len)
        eat_char(lexer);
    return true;
}

static inline struct loc make_loc(const struct lexer* lexer, const struct loc* loc) {
    return (struct loc) {
        .file  = loc->file,
        .begin = loc->begin,
        .end   = {
            .row = lexer->row,
            .col = lexer->col,
            .ptr = lexer->cur
        }
    };
}

static inline struct tok make_tok(const struct lexer* lexer, const char* begin, const struct loc* loc, unsigned tag) {
    return (struct tok) {
        .tag   = tag,
        .begin = begin,
        .end   = lexer->cur,
        .loc   = make_loc(lexer, loc)
    };
}

static size_t parse_index(struct lexer* lexer) {
    const char* begin = lexer->cur;
    while (lexer->cur != lexer->end && isdigit(*lexer->cur))
        eat_char(lexer);

    // Need to copy the range of characters and
    // add a terminating null character for strtoull.
    COPY_STR(index_str, begin, lexer->cur)
    size_t index = strtoull(index_str, NULL, 10);
    FREE_BUF(index_str);
    return index;
}

static struct tok lex(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);
        if (lexer->cur == lexer->end)
            return (struct tok) { .tag = TOK_EOF };

        const char* begin = lexer->cur;
        struct loc loc = (struct loc) {
            .file  = lexer->file,
            .begin = {
                .row = lexer->row,
                .col = lexer->col,
                .ptr = begin
            }
        };

        // Symbols
        if (accept_char(lexer, '(')) return make_tok(lexer, begin, &loc, TOK_LPAREN);
        if (accept_char(lexer, ')')) return make_tok(lexer, begin, &loc, TOK_RPAREN);
        if (accept_char(lexer, '_')) return make_tok(lexer, begin, &loc, TOK_WILD);

        // Keywords
        if (accept_str(lexer, "abs"))   return make_tok(lexer, begin, &loc, TOK_ABS);
        if (accept_str(lexer, "app"))   return make_tok(lexer, begin, &loc, TOK_APP);
        if (accept_str(lexer, "bot"))   return make_tok(lexer, begin, &loc, TOK_BOT);
        if (accept_str(lexer, "fvar"))  return make_tok(lexer, begin, &loc, TOK_FVAR);
        if (accept_str(lexer, "int"))   return make_tok(lexer, begin, &loc, TOK_INT);
        if (accept_str(lexer, "inj"))   return make_tok(lexer, begin, &loc, TOK_INJ);
        if (accept_str(lexer, "let"))   return make_tok(lexer, begin, &loc, TOK_LET);
        if (accept_str(lexer, "match")) return make_tok(lexer, begin, &loc, TOK_MATCH);
        if (accept_str(lexer, "pi"))    return make_tok(lexer, begin, &loc, TOK_PI);
        if (accept_str(lexer, "prod"))  return make_tok(lexer, begin, &loc, TOK_PROD);
        if (accept_str(lexer, "real"))  return make_tok(lexer, begin, &loc, TOK_REAL);
        if (accept_str(lexer, "star"))  return make_tok(lexer, begin, &loc, TOK_STAR);
        if (accept_str(lexer, "sum"))   return make_tok(lexer, begin, &loc, TOK_SUM);
        if (accept_str(lexer, "top"))   return make_tok(lexer, begin, &loc, TOK_TOP);
        if (accept_str(lexer, "tup"))   return make_tok(lexer, begin, &loc, TOK_TUP);
        if (accept_str(lexer, "uni"))   return make_tok(lexer, begin, &loc, TOK_UNI);

        // Identifiers
        if (isalpha(*lexer->cur)) {
            eat_char(lexer);
            while (isalnum(*lexer->cur) || *lexer->cur == '_')
                eat_char(lexer);
            return make_tok(lexer, begin, &loc, TOK_ID);
        }

        // Literals
        if (accept_str(lexer, "-0x") || accept_str(lexer, "0x")) {
            bool minus = *begin == '-';
            while (lexer->cur != lexer->end && isxdigit(*lexer->cur))
                eat_char(lexer);
            bool dot = false;
            if ((dot = accept_char(lexer, '.'))) {
                while (lexer->cur != lexer->end && isxdigit(*lexer->cur))
                    eat_char(lexer);
            }
            bool p = false;
            if ((p = accept_char(lexer, 'p'))) {
                while (lexer->cur != lexer->end && isdigit(*lexer->cur))
                    eat_char(lexer);
            }

            // Floating point literals must end with 'p'
            if ((minus || dot) && !p)
                goto error;

            // Make a null-terminated string for strtod and friends
            COPY_STR(str, begin, lexer->cur)
            union lit lit;
            if (!p)
                lit.int_val = strtoumax(str, NULL, 16);
            else
                lit.real_val = strtod(str, NULL);
            if (errno)
                goto error;
            FREE_BUF(str);
            return (struct tok) {
                .tag   = p ? TOK_REAL_LIT : TOK_INT_LIT,
                .begin = begin,
                .end   = lexer->cur,
                .lit   = lit,
                .loc   = make_loc(lexer, &loc)
            };
        }

        // De Bruijn indices
        if (accept_char(lexer, '#')) {
            size_t index = parse_index(lexer);
            if (!accept_char(lexer, '.'))
                goto error;
            size_t sub_index = parse_index(lexer);
            return (struct tok) {
                .tag      = TOK_DEBRUIJN,
                .begin    = begin,
                .end      = lexer->cur,
                .loc      = make_loc(lexer, &loc),
                .debruijn = {
                    .index     = index,
                    .sub_index = sub_index
                }
            };
        }

        eat_char(lexer);
error:
        loc = make_loc(lexer, &loc);
        COPY_STR(str, begin, lexer->cur)
        print_msg(lexer->log, MSG_ERR, &loc, "invalid token '{0:s}'", FMT_ARGS({ .s = str }));
        FREE_BUF(str);
        return make_tok(lexer, begin, &loc, TOK_ERR);
    }
}

static void push_env(parser_t parser) {
    size_t size = VEC_SIZE(parser->env.exps);
    VEC_PUSH(parser->env.levels, size);
}

static void push_exp(parser_t parser, exp_t exp) {
    VEC_PUSH(parser->env.exps, exp);
}

static void pop_env(parser_t parser) {
    assert(VEC_SIZE(parser->env.levels) > 0);
    size_t last = parser->env.levels[VEC_SIZE(parser->env.levels) - 1];
    RESIZE_VEC(parser->env.exps, last);
    VEC_POP(parser->env.levels);
}

static exp_t get_type(parser_t parser, size_t index, size_t sub_index) {
    if (VEC_SIZE(parser->env.levels) < index + 1)
        return NULL;
    size_t last  = VEC_SIZE(parser->env.levels) - index - 1;
    size_t begin = parser->env.levels[last];
    size_t end   = index == 0 ? VEC_SIZE(parser->env.exps) : parser->env.levels[last + 1];
    if (sub_index > end - begin)
        return NULL;
    return parser->env.exps[sub_index];
}

static void eat_tok(parser_t parser, unsigned tok) {
    assert(parser->ahead.tag == tok);
    (void)tok;
    parser->prev_loc = parser->ahead.loc;
    parser->ahead = lex(&parser->lexer);
}

static bool accept_tok(parser_t parser, unsigned tok) {
    if (parser->ahead.tag == tok) {
        eat_tok(parser, tok);
        return true;
    }
    return false;
}

static void expect_tok(parser_t parser, unsigned tok) {
    if (accept_tok(parser, tok))
        return;
    COPY_STR(str, parser->ahead.begin, parser->ahead.end)
    const char* quote =
        tok != TOK_INT_LIT &&
        tok != TOK_REAL_LIT &&
        tok != TOK_ID &&
        tok != TOK_DEBRUIJN
        ? "'" : "";
    print_msg(
        parser->lexer.log, MSG_ERR, &parser->ahead.loc,
        "expected %0:s%1:s%2:s, but got '%3:s'",
        FMT_ARGS({ .s = quote }, { .s = tok_to_str(tok) }, { .s = quote }, { .s = str }));
    FREE_BUF(str);
    eat_tok(parser, parser->ahead.tag);
}

static exp_t make_uni(parser_t parser) {
    return parser->uni = parser->uni
        ? parser->uni
        : import_exp(parser->mod, &(struct exp) { .tag = EXP_UNI });
}

static exp_t make_star(parser_t parser) {
    return parser->star = parser->star
        ? parser->star
        : import_exp(parser->mod, &(struct exp) { .tag = EXP_STAR, .type = make_uni(parser) });
}

static exp_t invalid_exp(parser_t parser, exp_t exp, const char* msg) {
    print_msg(parser->lexer.log, MSG_ERR, &exp->loc, "invalid %0:s", FMT_ARGS({ .s = msg }));
    return NULL;
}

static exp_t invalid_debruijn(parser_t parser, const struct loc* loc, size_t index, size_t sub_index) {
    print_msg(
        parser->lexer.log, MSG_ERR, loc,
        "invalid De Bruijn index '#%0:u.%1:u'",
        FMT_ARGS({ .u = index }, { .u = sub_index }));
    return NULL;
}

static exp_t generic_error(parser_t parser, const char* msg) {
    COPY_STR(str, parser->ahead.begin, parser->ahead.end)
    print_msg(
        parser->lexer.log, MSG_ERR, &parser->ahead.loc,
        "expected %0:s, but got '%1:s'",
        FMT_ARGS({ .s = msg }, { .s = str }));
    FREE_BUF(str);
    return NULL;
}

static exp_t parse_uni(parser_t parser) {
    eat_tok(parser, TOK_UNI);
    return make_uni(parser);
}

static exp_t parse_star(parser_t parser) {
    eat_tok(parser, TOK_STAR);
    return make_star(parser);
}

static exp_t parse_bvar(parser_t parser) {
    size_t index     = parser->ahead.debruijn.index;
    size_t sub_index = parser->ahead.debruijn.sub_index;
    struct loc loc   = parser->ahead.loc;
    eat_tok(parser, TOK_DEBRUIJN);
    exp_t type = get_type(parser, index, sub_index);
    if (!type) 
        return invalid_debruijn(parser, &loc, index, sub_index);
    return import_exp(parser->mod, &(struct exp) {
        .tag  = EXP_BVAR,
        .type = type,
        .bvar = {
            .index     = index,
            .sub_index = sub_index
        }
    });
}

static exp_t* parse_exps(parser_t parser) {
    exp_t* exps = NEW_VEC(exp_t);
    while (
        parser->ahead.tag == TOK_LPAREN ||
        parser->ahead.tag == TOK_UNI ||
        parser->ahead.tag == TOK_STAR ||
        parser->ahead.tag == TOK_DEBRUIJN)
    {
        exp_t exp = parse_exp(parser);
        if (!exp) {
            FREE_VEC(exps);
            return NULL;
        }
        VEC_PUSH(exps, exp);
    }
    return exps;
}

static exp_t parse_paren_exp(parser_t parser) {
    switch (parser->ahead.tag) {
        case TOK_ABS: {
            eat_tok(parser, TOK_ABS);
            exp_t type = parse_exp(parser);
            push_env(parser);
            if (type && type->tag != EXP_PI)
                return invalid_exp(parser, type, "abstraction type");
            push_exp(parser, type->pi.dom);
            exp_t body = parse_exp(parser);
            pop_env(parser);
            return body && type ? import_exp(parser->mod, &(struct exp) {
                .tag      = EXP_ABS,
                .type     = type,
                .abs.body = body
            }) : NULL;
        }
        case TOK_APP: {
            eat_tok(parser, TOK_APP);
            exp_t type  = parse_exp(parser);
            exp_t left  = parse_exp(parser);
            exp_t right = parse_exp(parser);
            return type && left && right ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_APP,
                .type = type,
                .app  = {
                    .left  = left,
                    .right = right
                }
            }) : NULL;
        }
        case TOK_PI: {
            eat_tok(parser, TOK_PI);
            exp_t dom = parse_exp(parser);
            push_env(parser);
            push_exp(parser, dom);
            exp_t codom = parse_exp(parser);
            pop_env(parser);
            if (!codom->type)
                return invalid_exp(parser, codom, "pi codomain");
            return dom && codom ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_PI,
                .type = codom->type,
                .pi   = {
                    .dom   = dom,
                    .codom = codom
                }
            }) : NULL;
        }
        case TOK_BOT:
        case TOK_TOP: {
            eat_tok(parser, parser->ahead.tag);
            exp_t type = parse_exp(parser);
            return type ? import_exp(parser->mod, &(struct exp) {
                .tag  = parser->ahead.tag == TOK_BOT ? EXP_BOT : EXP_TOP,
                .type = type
            }) : NULL;
        }
        case TOK_SUM:
        case TOK_PROD:
        case TOK_TUP: {
            unsigned tag =
                parser->ahead.tag == TOK_SUM  ? EXP_SUM  :
                parser->ahead.tag == TOK_PROD ? EXP_PROD :
                EXP_TUP;
            eat_tok(parser, parser->ahead.tag);
            exp_t type = parse_exp(parser);
            exp_t* args = parse_exps(parser);
            if (!args)
                return NULL;
            exp_t exp = type ? import_exp(parser->mod, &(struct exp) {
                .tag  = tag,
                .type = type,
                .tup  = {
                    .args      = args,
                    .arg_count = VEC_SIZE(args)
                }
            }) : NULL;
            FREE_VEC(args);
            return exp;
        }
        case TOK_LET: {
            eat_tok(parser, TOK_LET);
            expect_tok(parser, TOK_LPAREN);
            exp_t* binds = parse_exps(parser);
            expect_tok(parser, TOK_RPAREN);
            exp_t body = parse_exp(parser);
            if (!binds)
                return NULL;
            if (!body->type) {
                FREE_VEC(binds);
                return invalid_exp(parser, body, "let-statement body");
            }
            exp_t exp = import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_LET,
                .type = body->type,
                .let  = {
                    .body       = body,
                    .binds      = binds,
                    .bind_count = VEC_SIZE(binds)
                }
            });
            FREE_VEC(binds);
            return exp;
        }
        case TOK_INJ: {
            eat_tok(parser, TOK_INJ);
            exp_t type = parse_exp(parser);
            size_t index = 0;
            if (parser->ahead.tag == TOK_INT_LIT)
                index = parser->ahead.lit.int_val;
            expect_tok(parser, TOK_INT_LIT);
            exp_t arg = parse_exp(parser);
            return type && arg ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_INJ,
                .type = type,
                .inj  = {
                    .index = index,
                    .arg   = arg
                }
            }) : NULL;
        }
        case TOK_FVAR: {
            eat_tok(parser, TOK_FVAR);
            exp_t type = parse_exp(parser);
            COPY_STR(str, parser->ahead.begin, parser->ahead.end)
            const char* name = NULL;
            if (parser->ahead.tag == TOK_ID)
                name = str;
            expect_tok(parser, TOK_ID);
            exp_t exp = name && type ? import_exp(parser->mod, &(struct exp) {
                .tag       = EXP_FVAR,
                .type      = type,
                .fvar.name = name
            }) : NULL;
            FREE_BUF(str);
            return exp;
        }
        // EXP_INT
        // EXP_REAL
        // EXP_LIT
        // EXP_MATCH
        default:
            return generic_error(parser, "parenthesized expression contents");
    }
}

exp_t parse_exp(parser_t parser) {
    struct loc loc = parser->ahead.loc;
    exp_t exp = NULL;
    switch (parser->ahead.tag) {
        case TOK_LPAREN: {
            eat_tok(parser, TOK_LPAREN);
            exp = parse_paren_exp(parser);
            expect_tok(parser, TOK_RPAREN);
            break;
        }
        case TOK_DEBRUIJN: exp = parse_bvar(parser); break;
        case TOK_UNI:      exp = parse_uni(parser);  break;
        case TOK_STAR:     exp = parse_star(parser); break;
        default:
            return generic_error(parser, "expression");
    }
    if (exp) {
        ((struct exp*)exp)->loc = (struct loc) {
            .file  = loc.file,
            .begin = loc.begin,
            .end   = parser->prev_loc.end
        };
    }
    return exp;
}
