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
    f(LIT, "<literal>") \
    f(WILD, "_") \
    f(DEBRUIJN, "<de-bruijn index>") \
    f(ID, "<identifier>") \
    f(ERR, "<error>") \
    f(EOF, "<end-of-file>")

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

static inline struct loc front_loc(const struct lexer* lexer) {
    return (struct loc) {
        .file  = lexer->file,
        .begin = {
            .row = lexer->row,
            .col = lexer->col,
            .ptr = lexer->cur
        }
    };
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
    FREE_BUF(index_str)
    return index;
}

static struct tok lex(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);
        if (lexer->cur == lexer->end)
            return (struct tok) { .tag = TOK_EOF };

        const char* begin = lexer->cur;
        struct loc loc = front_loc(lexer);

        // Symbols
        if (accept_char(lexer, '(')) return make_tok(lexer, begin, &loc, TOK_LPAREN);
        if (accept_char(lexer, ')')) return make_tok(lexer, begin, &loc, TOK_RPAREN);
        if (accept_char(lexer, '_')) return make_tok(lexer, begin, &loc, TOK_WILD);

        // Keywords
        if (accept_str(lexer, "abs"))   return make_tok(lexer, begin, &loc, TOK_ABS);
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
            FREE_BUF(str)
            return (struct tok) {
                .tag   = TOK_LIT,
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
        FREE_BUF(str)
        return make_tok(lexer, begin, &loc, TOK_ERR);
    }
}

static void push_env(parser_t parser) {
    size_t size = VEC_SIZE(parser->env.exps);
    PUSH_TO_VEC(parser->env.levels, size);
}

static void push_exp(parser_t parser, exp_t exp) {
    PUSH_TO_VEC(parser->env.exps, exp);
}

static void pop_env(parser_t parser) {
    assert(VEC_SIZE(parser->env.levels) > 0);
    size_t last = parser->env.levels[VEC_SIZE(parser->env.levels) - 1];
    RESIZE_VEC(parser->env.exps, last);
    POP_FROM_VEC(parser->env.levels);
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
    print_msg(
        parser->lexer.log, MSG_ERR, &parser->ahead.loc,
        "expected '%0:s', but got '%1:s'",
        FMT_ARGS({ .s = tok_to_str(tok) }, { .s = str }));
    FREE_BUF(str)
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

static exp_t parse_error(parser_t parser, const char* msg) {
    COPY_STR(str, parser->ahead.begin, parser->ahead.end)
    print_msg(
        parser->lexer.log, MSG_ERR, &parser->ahead.loc,
        "expected %0:s, but got '%1:s'",
        FMT_ARGS({ .s = msg }, { .s = str }));
    FREE_BUF(str)
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
    eat_tok(parser, TOK_DEBRUIJN);
    exp_t type = get_type(parser, index, sub_index);
    if (!type) {
        print_msg(
            parser->lexer.log, MSG_ERR, &parser->ahead.loc,
            "invalid De Bruijn index '#%0:u.%1:u'",
            FMT_ARGS({ .u = index }, { .u = sub_index }));
        return NULL;
    }
    return import_exp(parser->mod, &(struct exp) {
        .tag  = EXP_BVAR,
        .type = type,
        .bvar = {
            .index     = index,
            .sub_index = sub_index
        }
    });
}

static exp_t parse_paren_exp(parser_t parser) {
    switch (parser->ahead.tag) {
        case TOK_ABS: {
            eat_tok(parser, TOK_ABS);
            exp_t dom = parse_exp(parser);
            push_env(parser);
            push_exp(parser, dom);
            exp_t body = parse_exp(parser);
            if (body && (!body->type || !body->type->type)) {
                print_msg(parser->lexer.log, MSG_ERR, &body->loc, "invalid abstraction body", NULL);
                return NULL;
            }
            pop_env(parser);
            return body && dom ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_ABS,
                .type = import_exp(parser->mod, &(struct exp) {
                    .tag  = EXP_PI,
                    .type = body->type->type,
                    .pi   = {
                        .dom   = dom,
                        .codom = body->type
                    }
                }),
                .abs.body = body
            }) : NULL;
        }
        case TOK_PI: {
            eat_tok(parser, TOK_PI);
            exp_t dom = parse_exp(parser);
            push_env(parser);
            push_exp(parser, dom);
            exp_t codom = parse_exp(parser);
            pop_env(parser);
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
            eat_tok(parser, parser->ahead.tag);
            exp_t type = parse_exp(parser);
            NEW_STACK_VEC(args, exp_t)
            while (
                parser->ahead.tag == TOK_LPAREN ||
                parser->ahead.tag == TOK_UNI ||
                parser->ahead.tag == TOK_STAR ||
                parser->ahead.tag == TOK_DEBRUIJN)
            {
                exp_t arg = parse_exp(parser);
                if (!arg) {
                    FREE_VEC(args);
                    return NULL;
                }
                PUSH_TO_VEC(args, arg);
            }
            unsigned tag =
                parser->ahead.tag == TOK_SUM  ? EXP_SUM  :
                parser->ahead.tag == TOK_PROD ? EXP_PROD :
                EXP_TUP;
            exp_t exp = import_exp(parser->mod, &(struct exp) {
                .tag  = tag,
                .type = type,
                .tup  = {
                    .args      = args,
                    .arg_count = VEC_SIZE(args)
                }
            });
            FREE_VEC(args);
            return exp;
        }
        // TODO
        default:
            return parse_error(parser, "parenthesized expresion");
    }
}

exp_t parse_exp(parser_t parser) {
    struct loc loc = front_loc(&parser->lexer);
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
            return parse_error(parser, "expression");
    }
    if (exp)
        ((struct exp*)exp)->loc = make_loc(&parser->lexer, &loc);
    return exp;
}