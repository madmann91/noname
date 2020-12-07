#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include "utils/format.h"
#include "utils/utils.h"
#include "utils/set.h"
#include "utils/vec.h"
#include "utils/utf8.h"
#include "utils/buf.h"
#include "log.h"
#include "parse.h"

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
    f(INS, "ins") \
    f(EXT, "ext") \
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

#define COPY_STR(str, begin, end) \
    size_t str##_len = end - begin; \
    char* str = new_buf(char, str##_len + 1); \
    memcpy(str, begin, str##_len); str[str##_len] = 0;

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

static inline bool compare_var(const void*, const void*);
static inline uint32_t hash_var(const void*);
CUSTOM_SET(var_set, exp_t, hash_var, compare_var)

struct parser {
    mod_t mod;
    struct lexer lexer;
    struct tok ahead;
    struct pos prev_end;
    struct var_set visible_vars;
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
        if (accept_str(lexer, "ins"))    return make_tok(lexer, begin, &pos, TOK_INS);
        if (accept_str(lexer, "ext"))    return make_tok(lexer, begin, &pos, TOK_EXT);
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
            free_buf(str);
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
            struct tok tok = make_tok(lexer, begin, &pos, TOK_INT_VAL);
            tok.int_val = strtoull(str, NULL, 10);
            free_buf(str);
            return tok;
        }

        eat_char(lexer);
error:
        {
            struct tok tok = make_tok(lexer, begin, &pos, TOK_ERR);
            COPY_STR(str, begin, lexer->cur)
            log_error(lexer->log, &tok.loc, "invalid token '{0:s}'", FMT_ARGS({ .s = str }));
            free_buf(str);
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
    free_buf(str);
    eat_tok(parser, parser->ahead.tag);
}

static struct exp_vec parse_many(parser_t parser, exp_t (*parse_one)(parser_t)) {
    struct exp_vec exps = new_exp_vec();
    while (
        parser->ahead.tag == TOK_LPAREN ||
        parser->ahead.tag == TOK_UNI ||
        parser->ahead.tag == TOK_STAR ||
        parser->ahead.tag == TOK_NAT ||
        parser->ahead.tag == TOK_HASH)
    {
        exp_t exp = parse_one(parser);
        if (!exp)
            break;
        push_to_exp_vec(&exps, exp);
    }
    return exps;
}

static inline struct loc make_loc(parser_t parser, struct pos begin) {
    return (struct loc) {
        .file = parser->lexer.file,
        .begin = begin,
        .end = parser->prev_end
    };
}

// Error messages ------------------------------------------------------------------

static exp_t expect_element(parser_t parser, const char* msg) {
    COPY_STR(str, parser->ahead.begin, parser->ahead.end)
    log_error(
        parser->lexer.log, &parser->ahead.loc,
        "expected %0:s, but got '%1:s'",
        FMT_ARGS({ .s = msg }, { .s = str }));
    free_buf(str);
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

static inline bool compare_var(const void* ptr1, const void* ptr2) {
    return (*(exp_t*)ptr1)->var.index == (*(exp_t*)ptr2)->var.index;
}

static inline uint32_t hash_var(const void* ptr) {
    return hash_uint(FNV_OFFSET, (*(exp_t*)ptr)->var.index);
}

static inline exp_t find_var(parser_t parser, size_t index) {
    return deref_or_null((void**)find_in_var_set(&parser->visible_vars, &(struct exp) { .var.index = index }));
}

static inline void declare_var(parser_t parser, exp_t var) {
    if (!insert_in_var_set(&parser->visible_vars, var))
        invalid_var(parser, &var->loc, var->var.index);
}

static inline void forget_var(parser_t parser, exp_t var) {
    bool ok = remove_from_var_set(&parser->visible_vars, var);
    assert(ok); (void)ok;
}

// Parsing functions ---------------------------------------------------------------

static inline size_t parse_index(parser_t parser) {
    size_t index = parser->ahead.int_val;
    expect_tok(parser, TOK_INT_VAL);
    return index;
}

static exp_t parse_exp_or_pat(parser_t, bool);

static exp_t parse_pat(parser_t parser) { return parse_exp_or_pat(parser, true); }
/*  */ exp_t parse_exp(parser_t parser) { return parse_exp_or_pat(parser, false); }
static struct exp_vec parse_exps(parser_t parser) { return parse_many(parser, parse_exp); }
static struct exp_vec parse_pats(parser_t parser) { return parse_many(parser, parse_pat); }

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
    expect_tok(parser, TOK_HASH);
    size_t index = parse_index(parser);
    expect_tok(parser, TOK_COLON);
    exp_t type = parse_exp(parser);
    struct loc loc = make_loc(parser, begin);
    return new_var(parser->mod, type, index, &loc);
}

static inline exp_t parse_paren_var_decl(parser_t parser) {
    expect_tok(parser, TOK_LPAREN);
    exp_t var = parse_var_decl(parser);
    expect_tok(parser, TOK_RPAREN);
    return var;
}

static exp_t parse_let(parser_t parser) {
    struct pos begin = parser->ahead.loc.begin;
    bool rec = parser->ahead.tag == TOK_LETREC;
    eat_tok(parser, parser->ahead.tag);

    expect_tok(parser, TOK_LPAREN);
    struct exp_vec vars = new_exp_vec();
    while (accept_tok(parser, TOK_LPAREN)) {
        exp_t var = parse_var_decl(parser);
        expect_tok(parser, TOK_RPAREN);
        if (rec) declare_var(parser, var);
        push_to_exp_vec(&vars, var);
    }
    expect_tok(parser, TOK_RPAREN);

    expect_tok(parser, TOK_LPAREN);
    struct exp_vec vals = parse_exps(parser);
    expect_tok(parser, TOK_RPAREN);

    if (!rec) {
        for (size_t i = 0, n = vars.size; i < n; ++i)
            declare_var(parser, vars.elems[i]);
    }
    exp_t body = parse_exp(parser);
    for (size_t i = 0, n = vars.size; i < n; ++i)
        forget_var(parser, vars.elems[i]);

    exp_t exp = NULL;
    if (!body)
        goto cleanup;
    struct loc loc = make_loc(parser, begin);
    if (vars.size != vals.size) {
        log_error(parser->lexer.log, &loc,
            "expected %0:u let-expression values(s), but got %1:u",
            FMT_ARGS({ .u = vars.size }, { .u = vals.size }));
        goto cleanup;
    }

    exp = rec
        ? new_letrec(parser->mod, vars.elems, vals.elems, vars.size, body, &loc)
        : new_let(parser->mod, vars.elems, vals.elems, vars.size, body, &loc);

cleanup:
    free_exp_vec(&vars);
    free_exp_vec(&vals);
    return exp;
}

static exp_t parse_match(parser_t parser) {
    struct pos begin = parser->ahead.loc.begin;
    eat_tok(parser, TOK_MATCH);
    expect_tok(parser, TOK_LPAREN);
    struct exp_vec pats = new_exp_vec();
    struct exp_vec vals = new_exp_vec();
    while (accept_tok(parser, TOK_LPAREN)) {
        expect_tok(parser, TOK_CASE);
        exp_t pat = parse_exp_or_pat(parser, true);
        vars_t bound_vars = is_pat(pat) ? collect_bound_vars(pat) : NULL;
        if (bound_vars) {
            for (size_t i = 0, n = bound_vars->count; i < n; ++i)
                declare_var(parser, bound_vars->vars[i]);
        }
        exp_t val = parse_exp(parser);
        if (bound_vars) {
            for (size_t i = 0, n = bound_vars->count; i < n; ++i)
                forget_var(parser, bound_vars->vars[i]);
        }
        expect_tok(parser, TOK_RPAREN);
        if (val && pat) {
            push_to_exp_vec(&pats, pat);
            push_to_exp_vec(&vals, val);
        }
    }
    expect_tok(parser, TOK_RPAREN);
    exp_t arg = parse_exp(parser);
    struct loc loc = make_loc(parser, begin);
    exp_t exp = NULL;
    if (!arg)
        goto cleanup;

    exp = new_match(parser->mod, pats.elems, vals.elems, pats.size, arg, &loc);

cleanup:
    free_exp_vec(&vals);
    free_exp_vec(&pats);
    return exp;
}

static exp_t parse_paren_exp_or_pat(parser_t parser, bool is_pat) {
    struct pos begin = parser->ahead.loc.begin;
    unsigned tag = parser->ahead.tag;
    switch (parser->ahead.tag) {
        case TOK_ABS: {
            eat_tok(parser, TOK_ABS);
            exp_t var = parse_paren_var_decl(parser);
            declare_var(parser, var);
            exp_t body = parse_exp(parser);
            forget_var(parser, var);
            struct loc loc = make_loc(parser, begin);
            return var && body ? new_abs(parser->mod, var, body, &loc) : NULL;
        }
        case TOK_PI: {
            eat_tok(parser, TOK_PI);
            exp_t var = parse_paren_var_decl(parser);
            exp_t dom = parse_exp(parser);
            exp_t codom = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return dom && codom ? new_pi(parser->mod, var, dom, codom, &loc) : NULL;
        }
        case TOK_WILD: {
            eat_tok(parser, TOK_WILD);
            exp_t type = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return type ? new_wild(parser->mod, type, &loc) : NULL;
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
            is_pat = false;
            // fallthrough
        case TOK_TUP: {
            eat_tok(parser, parser->ahead.tag);
            struct exp_vec args = is_pat ? parse_pats(parser) : parse_exps(parser);
            struct loc loc = make_loc(parser, begin);
            exp_t exp =
                tag == TOK_SUM  ?  new_sum (parser->mod, args.elems, args.size, &loc) :
                tag == TOK_PROD ?  new_prod(parser->mod, args.elems, args.size, &loc) :
                /*tag == TOK_TUP*/ new_tup (parser->mod, args.elems, args.size, &loc);
            free_exp_vec(&args);
            return exp;
        }
        case TOK_LET:
        case TOK_LETREC:
            return parse_let(parser);
        case TOK_INJ: {
            eat_tok(parser, TOK_INJ);
            exp_t type = parse_exp(parser);
            size_t index = parse_index(parser);
            exp_t arg = parse_exp_or_pat(parser, is_pat);
            struct loc loc = make_loc(parser, begin);
            return type && arg ? new_inj(parser->mod, type, index, arg, &loc) : NULL;
        }
        case TOK_INS: {
            eat_tok(parser, TOK_INS);
            exp_t val = parse_exp(parser);
            exp_t index = parse_exp(parser);
            exp_t elem = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return val && index && elem ? new_ins(parser->mod, val, index, elem, &loc) : NULL;
        }
        case TOK_EXT: {
            eat_tok(parser, TOK_EXT);
            exp_t val = parse_exp(parser);
            exp_t index = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return val && index ? new_ext(parser->mod, val, index, &loc) : NULL;
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
            struct loc loc = make_loc(parser, begin);
            return type ? new_lit(parser->mod, type, &lit, &loc) : NULL;
        }
        case TOK_MATCH:
            return parse_match(parser);
        case TOK_HASH:
            if (is_pat)
                return parse_var_decl(parser);
            // fallthrough
        default: {
            // Application
            exp_t left  = parse_exp(parser);
            exp_t right = parse_exp(parser);
            struct loc loc = make_loc(parser, begin);
            return left && right ? new_app(parser->mod, left, right, &loc) : NULL;
        }
    }
}

static exp_t parse_exp_or_pat(parser_t parser, bool is_pat) {
    switch (parser->ahead.tag) {
        case TOK_LPAREN: {
            eat_tok(parser, TOK_LPAREN);
            exp_t exp = parse_paren_exp_or_pat(parser, is_pat);
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
    parser->visible_vars = new_var_set();
    parser->ahead = lex(&parser->lexer);
    return parser;
}

void free_parser(parser_t parser) {
    free_var_set(&parser->visible_vars);
    free(parser);
}
