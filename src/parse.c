#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include "parse.h"
#include "format.h"
#include "utils.h"
#include "utf8.h"
#include "log.h"
#include "htable.h"
#include "hash.h"
#include "vec.h"

#define TOKENS(f) \
    f(LPAREN, "(") \
    f(RPAREN, ")") \
    f(HASH, "#") \
    f(COLON, ":") \
    f(ABS, "abs") \
    f(BOT, "bot") \
    f(CASE, "case") \
    f(INT, "int") \
    f(INJ, "inj") \
    f(LET, "let") \
    f(LETREC, "letrec") \
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

struct parser {
    mod_t mod;
    struct lexer lexer;
    struct tok ahead;
    struct pos prev_end;
    struct htable vars;
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

// Lexing helpers ------------------------------------------------------------------

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

static inline struct pos get_end_pos(const struct lexer* lexer) {
    return (struct pos) {
        .row = lexer->row,
        .col = lexer->col,
        .ptr = lexer->cur
    };
}

static inline struct tok make_tok(const struct lexer* lexer, const char* begin, const struct pos* pos, unsigned tag) {
    return (struct tok) {
        .tag = tag,
        .begin = begin,
        .end = lexer->cur,
        .loc = (struct loc) {
            .file = lexer->file,
            .begin = *pos,
            .end = get_end_pos(lexer)
        }
    };
}

// Lexer ---------------------------------------------------------------------------

static struct tok lex(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);
        if (lexer->cur == lexer->end)
            return (struct tok) { .tag = TOK_EOF };

        const char* begin = lexer->cur;
        struct pos pos = (struct pos) {
            .row = lexer->row,
            .col = lexer->col,
            .ptr = begin
        };

        // Symbols
        if (accept_char(lexer, '(')) return make_tok(lexer, begin, &pos, TOK_LPAREN);
        if (accept_char(lexer, ')')) return make_tok(lexer, begin, &pos, TOK_RPAREN);
        if (accept_char(lexer, '#')) return make_tok(lexer, begin, &pos, TOK_HASH);
        if (accept_char(lexer, ':')) return make_tok(lexer, begin, &pos, TOK_COLON);
        if (accept_char(lexer, ';')) {
            while (lexer->cur != lexer->end && *lexer->cur != '\n')
                eat_char(lexer);
            continue;
        }

        // Keywords
        if (accept_str(lexer, "abs"))    return make_tok(lexer, begin, &pos, TOK_ABS);
        if (accept_str(lexer, "bot"))    return make_tok(lexer, begin, &pos, TOK_BOT);
        if (accept_str(lexer, "case"))   return make_tok(lexer, begin, &pos, TOK_CASE);
        if (accept_str(lexer, "int"))    return make_tok(lexer, begin, &pos, TOK_INT);
        if (accept_str(lexer, "inj"))    return make_tok(lexer, begin, &pos, TOK_INJ);
        if (accept_str(lexer, "let")) {
            if (accept_str(lexer, "rec"))
                return make_tok(lexer, begin, &pos, TOK_LETREC);
            return make_tok(lexer, begin, &pos, TOK_LET);
        }
        if (accept_str(lexer, "lit"))    return make_tok(lexer, begin, &pos, TOK_LIT);
        if (accept_str(lexer, "match"))  return make_tok(lexer, begin, &pos, TOK_MATCH);
        if (accept_str(lexer, "nat"))    return make_tok(lexer, begin, &pos, TOK_NAT);
        if (accept_str(lexer, "pi"))     return make_tok(lexer, begin, &pos, TOK_PI);
        if (accept_str(lexer, "prod"))   return make_tok(lexer, begin, &pos, TOK_PROD);
        if (accept_str(lexer, "real"))   return make_tok(lexer, begin, &pos, TOK_REAL);
        if (accept_str(lexer, "star"))   return make_tok(lexer, begin, &pos, TOK_STAR);
        if (accept_str(lexer, "sum"))    return make_tok(lexer, begin, &pos, TOK_SUM);
        if (accept_str(lexer, "top"))    return make_tok(lexer, begin, &pos, TOK_TOP);
        if (accept_str(lexer, "tup"))    return make_tok(lexer, begin, &pos, TOK_TUP);
        if (accept_str(lexer, "uni"))    return make_tok(lexer, begin, &pos, TOK_UNI);
        if (accept_str(lexer, "wild"))   return make_tok(lexer, begin, &pos, TOK_WILD);

        // Identifiers
        if (isalpha(*lexer->cur)) {
            eat_char(lexer);
            while (isalnum(*lexer->cur) || *lexer->cur == '_')
                eat_char(lexer);
            return make_tok(lexer, begin, &pos, TOK_ID);
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
                if (!accept_char(lexer, '+'))
                    accept_char(lexer, '-');
                while (lexer->cur != lexer->end && isdigit(*lexer->cur))
                    eat_char(lexer);
            }

            // Floating point literals must end with 'p'
            if ((minus || dot) && !p)
                goto error;

            // Make a null-terminated string for strtod and friends
            COPY_STR(str, begin, lexer->cur)
            struct tok tok = make_tok(lexer, begin, &pos, p ? TOK_HEX_FLOAT : TOK_HEX_INT);
            if (p) {
                tok.hex_float = strtod(str, NULL);
                if (minus)
                    tok.hex_float = -tok.hex_float;
            } else {
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
        {
            struct tok tok = make_tok(lexer, begin, &pos, TOK_ERR);
            COPY_STR(str, begin, lexer->cur)
            log_error(lexer->log, &tok.loc, "invalid token '{0:s}'", FMT_ARGS({ .s = str }));
            FREE_BUF(str);
            return tok;
        }
    }
}

// Parsing helpers -----------------------------------------------------------------

static void eat_tok(parser_t parser, unsigned tok) {
    assert(parser->ahead.tag == tok);
    (void)tok;
    parser->prev_end = parser->ahead.loc.end;
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
    log_error(
        parser->lexer.log, &parser->ahead.loc,
        "expected %0:s%1:s%2:s, but got '%3:s'",
        FMT_ARGS({ .s = quote }, { .s = tok_to_str(tok) }, { .s = quote }, { .s = str }));
    FREE_BUF(str);
    eat_tok(parser, parser->ahead.tag);
}

static inline struct loc make_loc(parser_t parser, struct pos begin) {
    return (struct loc) {
        .file = parser->lexer.file,
        .begin = begin,
        .end = parser->prev_end
    };
}

// Error messages ------------------------------------------------------------------

static exp_t invalid_element(parser_t parser, const struct loc* loc, const char* msg) {
    log_error(parser->lexer.log, loc, "invalid %0:s", FMT_ARGS({ .s = msg }));
    return NULL;
}

static exp_t expect_element(parser_t parser, const char* msg) {
    COPY_STR(str, parser->ahead.begin, parser->ahead.end)
    log_error(
        parser->lexer.log, &parser->ahead.loc,
        "expected %0:s, but got '%1:s'",
        FMT_ARGS({ .s = msg }, { .s = str }));
    FREE_BUF(str);
    return NULL;
}

static exp_t invalid_var(parser_t parser, const struct loc* loc, size_t index) {
    log_error(
        parser->lexer.log, loc,
        "invalid variable index '#%0:u'",
        FMT_ARGS({ .u = index }));
    return NULL;
}

// Variable handling ---------------------------------------------------------------

static inline bool cmp_vars(const void* a, const void* b) {
    return (*(exp_t*)a)->var.index == (*(exp_t*)b)->var.index;
}

static inline uint32_t hash_var(exp_t var) {
    return hash_uint(FNV_OFFSET, var->var.index);
}

static inline exp_t find_var(parser_t parser, size_t index) {
    exp_t var = &(struct exp) { .tag = EXP_VAR, .var.index = index };
    exp_t* elem = find_in_htable(&parser->vars, &var, hash_var(var));
    return elem ? *elem : NULL;
}

static inline void declare_var(parser_t parser, exp_t var) {
    if (!insert_in_htable(&parser->vars, &var, hash_var(var), NULL))
        invalid_var(parser, &var->loc, var->var.index);
}

static inline void forget_var(parser_t parser, exp_t var) {
    exp_t* elem = find_in_htable(&parser->vars, &var, hash_var(var));
    assert(elem);
    remove_from_htable(&parser->vars, elem - (exp_t*)parser->vars.elems);
}

// Parsing functions ---------------------------------------------------------------

static inline size_t parse_index(parser_t parser) {
    size_t index = 0;
    if (parser->ahead.tag == TOK_INT_VAL)
        index = parser->ahead.int_val;
    expect_tok(parser, TOK_INT_VAL);
    return index;
}

static exp_t parse_var(parser_t parser) {
    // Parses the name of a previously declared variable
    struct pos begin = parser->ahead.loc.begin;
    eat_tok(parser, TOK_HASH);
    size_t index = parse_index(parser);
    exp_t var = find_var(parser, index);
    if (!var) {
        struct loc loc = make_loc(parser, begin);
        return invalid_var(parser, &loc, index);
    }
    return new_var(parser->mod, var->type, index, NULL);
}

static exp_t parse_var_decl(parser_t parser) {
    struct pos begin = parser->ahead.loc.begin;
    eat_tok(parser, TOK_LPAREN);
    expect_tok(parser, TOK_HASH);
    size_t index = parse_index(parser);
    expect_tok(parser, TOK_COLON);
    exp_t type = parse_exp(parser);
    expect_tok(parser, TOK_RPAREN);
    struct loc loc = make_loc(parser, begin);
    return new_var(parser->mod, type, index, &loc);
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

static exp_t parse_let(parser_t parser) {
    struct pos begin = parser->ahead.loc.begin;
    bool rec = parser->ahead.tag == TOK_LETREC;
    eat_tok(parser, parser->ahead.tag);

    expect_tok(parser, TOK_LPAREN);
    exp_t* vars = NEW_VEC(exp_t);
    while (parser->ahead.tag == TOK_LPAREN) {
        exp_t var = parse_var_decl(parser);
        if (rec) declare_var(parser, var);
        VEC_PUSH(vars, var);
    }
    expect_tok(parser, TOK_RPAREN);

    expect_tok(parser, TOK_LPAREN);
    exp_t* vals = parse_exps(parser);
    expect_tok(parser, TOK_RPAREN);

    if (!rec) {
        for (size_t i = 0, n = VEC_SIZE(vars); i < n; ++i)
            declare_var(parser, vars[i]);
    }
    exp_t body = parse_exp(parser);
    for (size_t i = 0, n = VEC_SIZE(vars); i < n; ++i)
        forget_var(parser, vars[i]);

    exp_t exp = NULL;
    if (!body || !vals)
        goto cleanup;
    if (!body->type) {
        invalid_element(parser, &body->loc, rec ? "letrec-expression body" : "let-expression body");
        goto cleanup;
    }
    struct loc loc = make_loc(parser, begin);
    if (VEC_SIZE(vars) != VEC_SIZE(vals)) {
        log_error(parser->lexer.log, &loc, "number of variables does not match values", NULL);
        goto cleanup;
    }

    exp = rec
        ? new_letrec(parser->mod, vars, vals, VEC_SIZE(vars), body, &loc)
        : new_let(parser->mod, vars, vals, VEC_SIZE(vars), body, &loc);

cleanup:
    if (vars) FREE_VEC(vars);
    if (vals) FREE_VEC(vals);
    return exp;
}

static exp_t parse_pat(parser_t parser) {
    exp_t exp = parse_exp(parser);
    return is_pat(exp) ? exp : invalid_element(parser, &exp->loc, "pattern");
}

static exp_t parse_match(parser_t parser) {
    struct pos begin = parser->ahead.loc.begin;
    eat_tok(parser, TOK_MATCH);
    expect_tok(parser, TOK_LPAREN);
    exp_t* pats = NEW_VEC(exp_t);
    exp_t* vals = NEW_VEC(exp_t);
    while (accept_tok(parser, TOK_LPAREN)) {
        expect_tok(parser, TOK_CASE);
        exp_t pat = parse_pat(parser);
        exp_t val = parse_exp(parser);
        expect_tok(parser, TOK_RPAREN);
        if (val && pat) {
            VEC_PUSH(pats, pat);
            VEC_PUSH(vals, val);
        }
    }
    expect_tok(parser, TOK_RPAREN);
    exp_t arg = parse_exp(parser);
    struct loc loc = make_loc(parser, begin);
    exp_t exp = NULL;
    if (!arg)
        goto cleanup;
    if (VEC_SIZE(vals) == 0) {
        log_error(parser->lexer.log, &loc, "empty match-expression case list", NULL);
        goto cleanup;
    }
    if (!vals[0]->type) {
        invalid_element(parser, &vals[0]->loc, "match-expression case value");
        goto cleanup;
    }

    exp = new_match(parser->mod, vals, pats, VEC_SIZE(pats), arg, &loc);

cleanup:
    FREE_VEC(vals);
    FREE_VEC(pats);
    return exp;
}

static exp_t parse_paren_exp(parser_t parser) {
    struct pos begin = parser->ahead.loc.begin;
    unsigned tag = parser->ahead.tag;
    switch (parser->ahead.tag) {
        case TOK_ABS: {
            eat_tok(parser, TOK_ABS);
            exp_t var = parse_var_decl(parser);
            declare_var(parser, var);
            exp_t body = parse_exp(parser);
            forget_var(parser, var);
            struct loc loc = make_loc(parser, begin);
            return var && body ? new_abs(parser->mod, var, body, &loc) : NULL;
        }
        case TOK_PI: {
            eat_tok(parser, TOK_PI);
            exp_t var = parse_var_decl(parser);
            exp_t dom = parse_exp(parser);
            exp_t codom = parse_exp(parser);
            if (!codom->type)
                return invalid_element(parser, &codom->loc, "pi codomain");
            struct loc loc = make_loc(parser, begin);
            return dom && codom ? new_pi(parser->mod, var, dom, codom, &loc) : NULL;
        }
        case TOK_WILD: {
            eat_tok(parser, TOK_WILD);
            exp_t type = parse_exp(parser);
            exp_t sub_pat = parser->ahead.tag == TOK_LPAREN ? parse_exp(parser) : NULL;
            struct loc loc = make_loc(parser, begin);
            return type ? new_wild(parser->mod, type, sub_pat, &loc) : NULL;
        }
        case TOK_BOT:
        case TOK_TOP: {
            eat_tok(parser, parser->ahead.tag);
            exp_t type = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return type ? (tag == TOK_TOP
                ? new_top(parser->mod, type, &loc)
                : new_bot(parser->mod, type, &loc)) : NULL;
        }
        case TOK_SUM:
        case TOK_PROD:
        case TOK_TUP: {
            eat_tok(parser, parser->ahead.tag);
            exp_t* args = parse_exps(parser);
            if (!args)
                return NULL;
            struct loc loc = make_loc(parser, begin);
            exp_t exp =
                tag == TOK_SUM  ?  new_sum(parser->mod, args, VEC_SIZE(args), &loc) :
                tag == TOK_PROD ?  new_prod(parser->mod, args, VEC_SIZE(args), &loc) :
                /*tag == TOK_TUP*/ new_tup(parser->mod, args, VEC_SIZE(args), &loc);
            FREE_VEC(args);
            return exp;
        }
        case TOK_LET:
        case TOK_LETREC:
            return parse_let(parser);
        case TOK_INJ: {
            eat_tok(parser, TOK_INJ);
            exp_t type = parse_exp(parser);
            size_t index = parse_index(parser);
            exp_t arg = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return type && arg ? new_inj(parser->mod, type, index, arg, &loc) : NULL;
        }
        case TOK_INT:
        case TOK_REAL: {
            eat_tok(parser, parser->ahead.tag);
            exp_t bitwidth = parse_exp(parser);
            if (!bitwidth)
                return NULL;
            struct loc loc = make_loc(parser, begin);
            return bitwidth ? (tag == TOK_INT
                ? new_int(parser->mod, bitwidth, &loc)
                : new_real(parser->mod, bitwidth, &loc)) : NULL;
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
                return expect_element(parser, "literal value");
            eat_tok(parser, parser->ahead.tag);
            if (type && type->tag != EXP_REAL && type->tag != EXP_INT && type->tag != EXP_NAT)
                return invalid_element(parser, &type->loc, "literal type");
            struct loc loc = make_loc(parser, begin);
            return type ? new_lit(parser->mod, type, &lit, &loc) : NULL;
        }
        case TOK_MATCH:
            return parse_match(parser);
        default: {
            // Application
            exp_t left  = parse_exp(parser);
            exp_t right = parse_exp(parser);
            if (left && left->type->tag != EXP_PI)
                return invalid_element(parser, &left->loc, "callee type");
            struct loc loc = make_loc(parser, begin);
            return left && right ? new_app(parser->mod, left, right, &loc) : NULL;
        }
    }
}

exp_t parse_exp(parser_t parser) {
    switch (parser->ahead.tag) {
        case TOK_LPAREN: {
            eat_tok(parser, TOK_LPAREN);
            exp_t exp = parse_paren_exp(parser);
            expect_tok(parser, TOK_RPAREN);
            return exp;
        }
        case TOK_HASH:
            return parse_var(parser);
        case TOK_UNI:
            eat_tok(parser, TOK_UNI);
            return new_uni(parser->mod);
        case TOK_STAR:
            eat_tok(parser, TOK_STAR);
            return new_star(parser->mod);
        case TOK_NAT:
            eat_tok(parser, TOK_NAT);
            return new_nat(parser->mod);
        default:
            return expect_element(parser, "expression");
    }
}

parser_t new_parser(mod_t mod, struct log* log, const char* file_name, const char* data, size_t data_size) {
    parser_t parser = xmalloc(sizeof(struct parser));
    parser->mod = mod;
    parser->lexer.file = file_name;
    parser->lexer.log = log;
    parser->lexer.cur = data;
    parser->lexer.end = data + data_size;
    parser->lexer.row = 1;
    parser->lexer.col = 1;
    parser->prev_end = (struct pos) { .row = 1, .col = 1 };
    parser->vars = new_htable(sizeof(exp_t), DEFAULT_CAP, cmp_vars);
    parser->ahead = lex(&parser->lexer);
    return parser;
}

void free_parser(parser_t parser) {
    free_htable(&parser->vars);
    free(parser);
}
