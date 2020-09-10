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
    f(HASH, "#") \
    f(DOT, ".") \
    f(ABS, "abs") \
    f(BOT, "bot") \
    f(CASE, "case") \
    f(FVAR, "fvar") \
    f(INT, "int") \
    f(INJ, "inj") \
    f(LET, "let") \
    f(LIT, "lit") \
    f(MATCH, "match") \
    f(NAT, "nat") \
    f(PI, "pi") \
    f(PROD, "prod") \
    f(REAL, "real") \
    f(STAR, "star") \
    f(SUM, "sum") \
    f(TOP, "top") \
    f(TUP, "tup") \
    f(UNI, "uni") \
    f(WILD, "wild") \
    f(INT_VAL, "integer value") \
    f(HEX_INT, "hexadecimal integer") \
    f(HEX_FLOAT, "hexadecimal floating-point number") \
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
        double    hex_float;
        uintmax_t hex_int;
        size_t    int_val;
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
    exp_t uni, star, nat;
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
    parser->uni = parser->star = parser->nat = NULL;
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
        if (accept_char(lexer, '#')) return make_tok(lexer, begin, &loc, TOK_HASH);
        if (accept_char(lexer, '.')) return make_tok(lexer, begin, &loc, TOK_DOT);
        if (accept_char(lexer, ';')) {
            while (lexer->cur != lexer->end && *lexer->cur != '\n')
                eat_char(lexer);
            continue;
        }

        // Keywords
        if (accept_str(lexer, "abs"))   return make_tok(lexer, begin, &loc, TOK_ABS);
        if (accept_str(lexer, "bot"))   return make_tok(lexer, begin, &loc, TOK_BOT);
        if (accept_str(lexer, "case"))  return make_tok(lexer, begin, &loc, TOK_CASE);
        if (accept_str(lexer, "fvar"))  return make_tok(lexer, begin, &loc, TOK_FVAR);
        if (accept_str(lexer, "int"))   return make_tok(lexer, begin, &loc, TOK_INT);
        if (accept_str(lexer, "inj"))   return make_tok(lexer, begin, &loc, TOK_INJ);
        if (accept_str(lexer, "let"))   return make_tok(lexer, begin, &loc, TOK_LET);
        if (accept_str(lexer, "lit"))   return make_tok(lexer, begin, &loc, TOK_LIT);
        if (accept_str(lexer, "match")) return make_tok(lexer, begin, &loc, TOK_MATCH);
        if (accept_str(lexer, "nat"))   return make_tok(lexer, begin, &loc, TOK_NAT);
        if (accept_str(lexer, "pi"))    return make_tok(lexer, begin, &loc, TOK_PI);
        if (accept_str(lexer, "prod"))  return make_tok(lexer, begin, &loc, TOK_PROD);
        if (accept_str(lexer, "real"))  return make_tok(lexer, begin, &loc, TOK_REAL);
        if (accept_str(lexer, "star"))  return make_tok(lexer, begin, &loc, TOK_STAR);
        if (accept_str(lexer, "sum"))   return make_tok(lexer, begin, &loc, TOK_SUM);
        if (accept_str(lexer, "top"))   return make_tok(lexer, begin, &loc, TOK_TOP);
        if (accept_str(lexer, "tup"))   return make_tok(lexer, begin, &loc, TOK_TUP);
        if (accept_str(lexer, "uni"))   return make_tok(lexer, begin, &loc, TOK_UNI);
        if (accept_str(lexer, "wild"))  return make_tok(lexer, begin, &loc, TOK_WILD);

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
            struct tok tok = {
                .begin = begin,
                .end   = lexer->cur,
                .loc   = make_loc(lexer, &loc)
            };
            if (p) {
                tok.tag = TOK_HEX_FLOAT;
                tok.hex_float = strtod(str, NULL);
                if (minus)
                    tok.hex_float = -tok.hex_float;
            } else {
                tok.tag = TOK_HEX_INT,
                tok.hex_int = strtoumax(str, NULL, 16);
            }
            FREE_BUF(str);
            if (errno)
                goto error;
            return tok;
        }

        if (isdigit(*lexer->cur)) {
            do {
                eat_char(lexer);
            } while (lexer->cur != lexer->end && isdigit(*lexer->cur));

            // Make a null-terminated string for strtoull
            COPY_STR(str, begin, lexer->cur)
            size_t int_val = strtoull(str, NULL, 10);
            FREE_BUF(str);
            return (struct tok) {
                .tag     = TOK_INT_VAL,
                .begin   = begin,
                .end     = lexer->cur,
                .int_val = int_val
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
    if (sub_index >= end - begin)
        return NULL;
    return shift_exp(0, parser->env.exps[begin + sub_index], index + 1, true);
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
        tok != TOK_HEX_FLOAT &&
        tok != TOK_HEX_INT &&
        tok != TOK_INT_VAL &&
        tok != TOK_ERR &&
        tok != TOK_EOF &&
        tok != TOK_ID
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
        : import_exp(parser->mod, &(struct exp) { .tag = EXP_UNI, .uni = { parser->mod } });
}

static exp_t make_star(parser_t parser) {
    return parser->star = parser->star
        ? parser->star
        : import_exp(parser->mod, &(struct exp) { .tag = EXP_STAR, .type = make_uni(parser) });
}

static exp_t make_nat(parser_t parser) {
    return parser->nat = parser->nat
        ? parser->nat
        : import_exp(parser->mod, &(struct exp) { .tag = EXP_NAT, .type = make_star(parser) });
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

static exp_t parse_nat(parser_t parser) {
    eat_tok(parser, TOK_NAT);
    return make_nat(parser);
}

static exp_t parse_bvar(parser_t parser) {
    struct loc loc = parser->ahead.loc;
    eat_tok(parser, TOK_HASH);
    size_t index = 0, sub_index = 0;
    if (parser->ahead.tag == TOK_INT_VAL)
        index = parser->ahead.int_val;
    expect_tok(parser, TOK_INT_VAL);
    expect_tok(parser, TOK_DOT);
    loc.end = parser->ahead.loc.end;
    if (parser->ahead.tag == TOK_INT_VAL)
        sub_index = parser->ahead.int_val;
    expect_tok(parser, TOK_INT_VAL);
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
        parser->ahead.tag == TOK_NAT ||
        parser->ahead.tag == TOK_HASH)
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
            exp_t dom = parse_exp(parser);
            if (!dom)
                return NULL;
            push_env(parser);
            push_exp(parser, dom);
            exp_t body = parse_exp(parser);
            pop_env(parser);
            if (!body)
                return NULL;
            if (!body->type || !body->type->type)
                return invalid_exp(parser, body, "abstraction body");
            exp_t type = import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_PI,
                .type = shift_exp(0, body->type, 1, false)->type,
                .pi   = {
                    .dom   = dom,
                    .codom = body->type
                }
            });
            return import_exp(parser->mod, &(struct exp) {
                .tag      = EXP_ABS,
                .type     = type,
                .abs.body = body
            });
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
                .type = shift_exp(0, open_exp(0, codom->type, &dom, 1), 1, false),
                .pi   = {
                    .dom   = dom,
                    .codom = codom
                }
            }) : NULL;
        }
        case TOK_WILD:
        case TOK_BOT:
        case TOK_TOP: {
            eat_tok(parser, parser->ahead.tag);
            exp_t type = parse_exp(parser);
            unsigned tag =
                parser->ahead.tag == TOK_BOT ? EXP_BOT :
                parser->ahead.tag == TOK_TOP ? EXP_TOP :
                EXP_WILD;
            return type ? import_exp(parser->mod, &(struct exp) {
                .tag  = tag,
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
            push_env(parser);
            if (binds) {
                for (size_t i = 0, n = VEC_SIZE(binds); i < n; ++i) {
                    if (binds[i]->type)
                        push_exp(parser, binds[i]->type);
                    else
                        invalid_exp(parser, binds[i], "let binding");
                }
            }
            exp_t body = parse_exp(parser);
            pop_env(parser);
            if (!binds)
                return NULL;
            exp_t exp = NULL;
            if (body && body->type) {
                exp = import_exp(parser->mod, &(struct exp) {
                    .tag  = EXP_LET,
                    .type = shift_exp(0, open_exp(0, body->type, binds, VEC_SIZE(binds)), 1, false),
                    .let  = {
                        .body       = body,
                        .binds      = binds,
                        .bind_count = VEC_SIZE(binds)
                    }
                });
            } else if (body)
                invalid_exp(parser, body, "let-statement body");
            FREE_VEC(binds);
            return exp;
        }
        case TOK_INJ: {
            eat_tok(parser, TOK_INJ);
            exp_t type = parse_exp(parser);
            size_t index = 0;
            if (parser->ahead.tag == TOK_INT_VAL)
                index = parser->ahead.int_val;
            expect_tok(parser, TOK_INT_VAL);
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
            size_t index = 0;
            if (parser->ahead.tag == TOK_INT_VAL)
                index = parser->ahead.int_val;
            expect_tok(parser, TOK_INT_VAL);
            return type ? import_exp(parser->mod, &(struct exp) {
                .tag        = EXP_FVAR,
                .type       = type,
                .fvar.index = index
            }) : NULL;
        }
        case TOK_INT:
        case TOK_REAL: {
            unsigned tag = parser->ahead.tag == TOK_INT ? EXP_INT : EXP_REAL;
            eat_tok(parser, parser->ahead.tag);
            exp_t bitwidth = parse_exp(parser);
            return bitwidth ? import_exp(parser->mod, &(struct exp) {
                .tag           = tag,
                .type          = make_star(parser),
                .int_.bitwidth = bitwidth
            }) : NULL;
        }
        case TOK_LIT: {
            eat_tok(parser, TOK_LIT);
            exp_t type = parse_exp(parser);
            union lit lit;
            if (parser->ahead.tag == TOK_HEX_INT)
                lit.int_val = parser->ahead.hex_int;
            else if (parser->ahead.tag == TOK_HEX_FLOAT)
                lit.real_val = parser->ahead.hex_float;
            else if (parser->ahead.tag == TOK_INT_VAL)
                lit.int_val = parser->ahead.int_val;
            else
                return generic_error(parser, "literal value");
            eat_tok(parser, parser->ahead.tag);
            if (type && type->tag != EXP_REAL && type->tag != EXP_INT && type->tag != EXP_NAT)
                return invalid_exp(parser, type, "literal type");
            return type ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_LIT,
                .type = type,
                .lit  = lit
            }) : NULL;
        }
        case TOK_MATCH: {
            eat_tok(parser, TOK_MATCH);
            expect_tok(parser, TOK_LPAREN);
            exp_t* exps = NEW_VEC(exp_t);
            exp_t* pats = NEW_VEC(exp_t);
            while (accept_tok(parser, TOK_LPAREN)) {
                expect_tok(parser, TOK_CASE);
                exp_t pat = parse_exp(parser);
                push_env(parser);
                exp_t exp = parse_exp(parser);
                pop_env(parser);
                expect_tok(parser, TOK_RPAREN);
                VEC_PUSH(exps, exp);
                VEC_PUSH(pats, pat);
            }
            if (VEC_SIZE(exps) == 0)
                generic_error(parser, "match-expression case");
            if (!exps[0]->type)
                invalid_exp(parser, exps[0], "match-expression case value");
            expect_tok(parser, TOK_RPAREN);
            exp_t arg = parse_exp(parser);
            exp_t exp = arg && exps[0]->type ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_MATCH,
                .type = shift_exp(0, exps[0]->type, 1, false),
                .match = {
                    .arg       = arg,
                    .exps      = exps,
                    .pats      = pats,
                    .pat_count = VEC_SIZE(exps)
                }
            }) : NULL;
            FREE_VEC(exps);
            FREE_VEC(pats);
            return exp;
        }
        default: {
            exp_t left  = parse_exp(parser);
            exp_t right = parse_exp(parser);
            if (left && left->type->tag != EXP_PI)
                return invalid_exp(parser, left, "callee type");
            return left && right ? import_exp(parser->mod, &(struct exp) {
                .tag  = EXP_APP,
                .type = shift_exp(0, open_exp(0, left->type->pi.codom, &right, 1), 1, false),
                .app  = {
                    .left  = left,
                    .right = right
                }
            }) : NULL;
        }
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
        case TOK_HASH: exp = parse_bvar(parser); break;
        case TOK_UNI:  exp = parse_uni(parser);  break;
        case TOK_STAR: exp = parse_star(parser); break;
        case TOK_NAT:  exp = parse_nat(parser);  break;
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
