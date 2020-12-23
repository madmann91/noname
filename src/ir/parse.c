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
#include "utils/log.h"
#include "utils/lexer.h"
#include "ir/parse.h"

#define SYMBOLS(f) \
    f(LPAREN, "(") \
    f(RPAREN, ")") \
    f(HASH, "#") \
    f(UNDERSCORE, "_") \
    f(COLON, ":")

#define KEYWORDS(f) \
    f(ABS, "abs") \
    f(BOT, "bot") \
    f(CASE, "case") \
    f(INJ, "inj") \
    f(INS, "ins") \
    f(EXT, "ext") \
    f(LET, "let") \
    f(LETREC, "letrec") \
    f(LIT, "lit") \
    f(MATCH, "match") \
    f(NAT, "nat") \
    f(FLOAT, "float") \
    f(INT, "int") \
    f(ARROW, "arrow") \
    f(PROD, "prod") \
    f(STAR, "star") \
    f(SUM, "sum") \
    f(TOP, "top") \
    f(TUP, "tup") \
    f(UNI, "uni")

#define SPECIAL(f) \
    f(INT_VAL, "integer value") \
    f(HEX_INT, "hexadecimal integer") \
    f(HEX_FLOAT, "hexadecimal floating-point number") \
    f(IDENT, "identifier") \
    f(ERR, "error") \
    f(EOF, "end-of-file")

#define TOKENS(f) \
    SYMBOLS(f) \
    KEYWORDS(f) \
    SPECIAL(f)

#define COPY_STR(str, begin, end) \
    size_t str##_len = (end) - (begin); \
    char* str = new_buf(char, str##_len + 1); \
    memcpy(str, (begin), str##_len); str[str##_len] = 0;

struct tok {
    enum {
#define f(name, str) TOK_##name,
        TOKENS(f)
#undef f
    } tag;
    union {
        double    hex_float;
        uintmax_t hex_int;
        size_t    int_val;
    };
    struct loc loc;
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

static struct tok lex(struct lexer* lexer) {
    while (true) {
        eat_spaces(lexer);

        struct pos begin = lexer->pos;
        if (lexer->pos.ptr == lexer->end)
            return make_tok(lexer, &begin, TOK_EOF);

        // Symbols
        if (accept_char(lexer, '(')) return make_tok(lexer, &begin, TOK_LPAREN);
        if (accept_char(lexer, ')')) return make_tok(lexer, &begin, TOK_RPAREN);
        if (accept_char(lexer, '#')) return make_tok(lexer, &begin, TOK_HASH);
        if (accept_char(lexer, ':')) return make_tok(lexer, &begin, TOK_COLON);
        if (accept_char(lexer, '_')) return make_tok(lexer, &begin, TOK_UNDERSCORE);
        if (accept_char(lexer, ';')) {
            while (lexer->pos.ptr != lexer->end && *lexer->pos.ptr != '\n')
                eat_char(lexer);
            continue;
        }

        // Keywords and identifiers
        if (*lexer->pos.ptr == '_' || isalpha(*lexer->pos.ptr)) {
            do {
                eat_char(lexer);
            } while (*lexer->pos.ptr == '_' || isalnum(*lexer->pos.ptr));
            unsigned* keyword = find_in_keywords(&lexer->keywords, (struct keyword) { begin.ptr, lexer->pos.ptr });
            return keyword ? make_tok(lexer, &begin, *keyword) : make_tok(lexer, &begin, TOK_IDENT);
        }

        // Literals
        if (accept_str(lexer, "-0x") || accept_str(lexer, "0x")) {
            bool minus = *begin.ptr == '-';
            while (lexer->pos.ptr != lexer->end && isxdigit(*lexer->pos.ptr))
                eat_char(lexer);
            bool dot = false;
            if ((dot = accept_char(lexer, '.'))) {
                while (lexer->pos.ptr != lexer->end && isxdigit(*lexer->pos.ptr))
                    eat_char(lexer);
            }
            bool p = false;
            if ((p = accept_char(lexer, 'p'))) {
                if (!accept_char(lexer, '+'))
                    accept_char(lexer, '-');
                while (lexer->pos.ptr != lexer->end && isdigit(*lexer->pos.ptr))
                    eat_char(lexer);
            }

            // Floating point literals must end with 'p'
            if ((minus || dot) && !p)
                goto error;

            // Make a null-terminated string for strtod and friends
            COPY_STR(str, begin.ptr, lexer->pos.ptr)
            struct tok tok = make_tok(lexer, &begin, p ? TOK_HEX_FLOAT : TOK_HEX_INT);
            if (p) {
                tok.hex_float = strtod(str, NULL);
                if (minus)
                    tok.hex_float = -tok.hex_float;
            } else {
                tok.hex_int = strtoumax(str, NULL, 16);
            }
            bool ok = errno == 0;
            free_buf(str);
            if (!ok) goto error;
            return tok;
        }

        if (isdigit(*lexer->pos.ptr)) {
            do {
                eat_char(lexer);
            } while (lexer->pos.ptr != lexer->end && isdigit(*lexer->pos.ptr));

            // Make a null-terminated string for strtoull
            COPY_STR(str, begin.ptr, lexer->pos.ptr)
            struct tok tok = make_tok(lexer, &begin, TOK_INT_VAL);
            tok.int_val = strtoull(str, NULL, 10);
            free_buf(str);
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

static void eat_tok(struct parser* parser, unsigned tag) {
    assert(parser->ahead.tag == tag);
    (void)tag;
    parser->prev_end = parser->ahead.loc.end;
    parser->ahead = lex(&parser->lexer);
}

static bool accept_tok(struct parser* parser, unsigned tag) {
    if (parser->ahead.tag == tag) {
        eat_tok(parser, tag);
        return true;
    }
    return false;
}

static void expect_tok(struct parser* parser, unsigned tag) {
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

static struct exp_vec parse_many(struct parser* parser, exp_t (*parse_one)(struct parser*)) {
    struct exp_vec exps = new_exp_vec();
    while (
        parser->ahead.tag == TOK_LPAREN ||
        parser->ahead.tag == TOK_UNI ||
        parser->ahead.tag == TOK_STAR ||
        parser->ahead.tag == TOK_NAT ||
        parser->ahead.tag == TOK_FLOAT ||
        parser->ahead.tag == TOK_INT ||
        parser->ahead.tag == TOK_HASH)
    {
        exp_t exp = parse_one(parser);
        if (!exp)
            break;
        push_to_exp_vec(&exps, exp);
    }
    return exps;
}

static inline struct loc make_loc(struct parser* parser, struct pos begin) {
    return (struct loc) {
        .file = parser->lexer.file,
        .begin = begin,
        .end = parser->prev_end
    };
}

// Variable handling ---------------------------------------------------------------

static exp_t invalid_var(struct parser* parser, const struct loc* loc, size_t index) {
    log_error(
        parser->lexer.log, loc,
        "invalid variable index '#%0:u'",
        FMT_ARGS({ .u = index }));
    return NULL;
}

static inline bool compare_var(const void* ptr1, const void* ptr2) {
    return (*(exp_t*)ptr1)->var.index == (*(exp_t*)ptr2)->var.index;
}

static inline uint32_t hash_var(const void* ptr) {
    return hash_uint(hash_init(), (*(exp_t*)ptr)->var.index);
}

static inline exp_t find_var(struct parser* parser, size_t index) {
    return deref_or_null((void**)find_in_var_set(&parser->visible_vars, &(struct exp) { .var.index = index }));
}

static inline void declare_var(struct parser* parser, exp_t var) {
    if (!insert_in_var_set(&parser->visible_vars, var))
        invalid_var(parser, &var->loc, var->var.index);
}

static inline void forget_var(struct parser* parser, exp_t var) {
    bool ok = remove_from_var_set(&parser->visible_vars, var);
    assert(ok); (void)ok;
}

// Parsing functions ---------------------------------------------------------------

static exp_t parse_exp_or_pat(struct parser*, bool);
static exp_t parse_pat(struct parser* parser) { return parse_exp_or_pat(parser, true); }
static exp_t parse_exp_internal(struct parser* parser) { return parse_exp_or_pat(parser, false); }
static struct exp_vec parse_exps(struct parser* parser) { return parse_many(parser, parse_exp_internal); }
static struct exp_vec parse_pats(struct parser* parser) { return parse_many(parser, parse_pat); }

static inline size_t parse_index(struct parser* parser) {
    size_t index = parser->ahead.int_val;
    expect_tok(parser, TOK_INT_VAL);
    return index;
}

static exp_t parse_err(struct parser* parser, const char* msg) {
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
    return NULL;
}

static exp_t parse_var(struct parser* parser) {
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

static exp_t parse_var_decl(struct parser* parser) {
    struct pos begin = parser->ahead.loc.begin;
    size_t index = SIZE_MAX;
    if (!accept_tok(parser, TOK_UNDERSCORE)) {
        expect_tok(parser, TOK_HASH);
        size_t index = parse_index(parser);
    }
    expect_tok(parser, TOK_COLON);
    exp_t type = parse_exp_internal(parser);
    struct loc loc = make_loc(parser, begin);
    return new_var(parser->mod, type, index, &loc);
}

static inline exp_t parse_paren_var_decl(struct parser* parser) {
    expect_tok(parser, TOK_LPAREN);
    exp_t var = parse_var_decl(parser);
    expect_tok(parser, TOK_RPAREN);
    return var;
}

static exp_t parse_let(struct parser* parser) {
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
    exp_t body = parse_exp_internal(parser);
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

static exp_t parse_match(struct parser* parser) {
    struct pos begin = parser->ahead.loc.begin;
    eat_tok(parser, TOK_MATCH);
    expect_tok(parser, TOK_LPAREN);
    struct exp_vec pats = new_exp_vec();
    struct exp_vec vals = new_exp_vec();
    while (accept_tok(parser, TOK_LPAREN)) {
        expect_tok(parser, TOK_CASE);
        exp_t pat = parse_pat(parser);
        vars_t bound_vars = is_pat(pat) ? collect_bound_vars(pat) : NULL;
        if (bound_vars) {
            for (size_t i = 0, n = bound_vars->count; i < n; ++i)
                declare_var(parser, bound_vars->vars[i]);
        }
        exp_t val = parse_exp_internal(parser);
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
    exp_t arg = parse_exp_internal(parser);
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

static exp_t parse_paren_exp_or_pat(struct parser* parser, bool is_pat) {
    struct pos begin = parser->ahead.loc.begin;
    unsigned tag = parser->ahead.tag;
    switch (parser->ahead.tag) {
        case TOK_ABS: {
            eat_tok(parser, TOK_ABS);
            exp_t var = parse_paren_var_decl(parser);
            declare_var(parser, var);
            exp_t body = parse_exp_internal(parser);
            forget_var(parser, var);
            struct loc loc = make_loc(parser, begin);
            return var && body ? new_abs(parser->mod, var, body, &loc) : NULL;
        }
        case TOK_ARROW: {
            eat_tok(parser, TOK_ARROW);
            exp_t var = parse_paren_var_decl(parser);
            exp_t codom = parse_exp_internal(parser);
            struct loc loc = make_loc(parser, begin);
            return var && codom ? new_arrow(parser->mod, var, codom, &loc) : NULL;
        }
        case TOK_BOT:
        case TOK_TOP: {
            eat_tok(parser, parser->ahead.tag);
            exp_t type = parse_exp_internal(parser);
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
            exp_t type = parse_exp_internal(parser);
            size_t index = parse_index(parser);
            exp_t arg = parse_exp_or_pat(parser, is_pat);
            struct loc loc = make_loc(parser, begin);
            return type && arg ? new_inj(parser->mod, type, index, arg, &loc) : NULL;
        }
        case TOK_INS: {
            eat_tok(parser, TOK_INS);
            exp_t val = parse_exp_internal(parser);
            exp_t index = parse_exp_internal(parser);
            exp_t elem = parse_exp_internal(parser);
            struct loc loc = make_loc(parser, begin);
            return val && index && elem ? new_ins(parser->mod, val, index, elem, &loc) : NULL;
        }
        case TOK_EXT: {
            eat_tok(parser, TOK_EXT);
            exp_t val = parse_exp_internal(parser);
            exp_t index = parse_exp_internal(parser);
            struct loc loc = make_loc(parser, begin);
            return val && index ? new_ext(parser->mod, val, index, &loc) : NULL;
        }
        case TOK_LIT: {
            eat_tok(parser, TOK_LIT);
            exp_t type = parse_exp_internal(parser);
            struct lit lit = { .tag = LIT_INT };
            if (parser->ahead.tag == TOK_HEX_INT)
                lit.int_val = parser->ahead.hex_int;
            else if (parser->ahead.tag == TOK_HEX_FLOAT)
                lit.float_val = parser->ahead.hex_float, lit.tag = LIT_FLOAT;
            else if (parser->ahead.tag == TOK_INT_VAL)
                lit.int_val = parser->ahead.int_val;
            else
                return parse_err(parser, "literal value");
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
            exp_t left  = parse_exp_internal(parser);
            exp_t right = parse_exp_internal(parser);
            struct loc loc = make_loc(parser, begin);
            return left && right ? new_app(parser->mod, left, right, &loc) : NULL;
        }
    }
}

static exp_t parse_exp_or_pat(struct parser* parser, bool is_pat) {
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
        case TOK_INT:
            eat_tok(parser, TOK_INT);
            return new_nat(parser->mod);
        case TOK_FLOAT:
            eat_tok(parser, TOK_FLOAT);
            return new_nat(parser->mod);
        default:
            return parse_err(parser, "expression");
    }
}

exp_t parse_exp(mod_t mod, struct log* log, const char* file_name, const char* data, size_t data_size) {
    struct parser parser = {
        .mod = mod,
        .lexer.file = file_name,
        .lexer.log = log,
        .lexer.pos = { .ptr = data, .row = 1, .col = 1 },
        .lexer.end = data + data_size,
        .prev_end = { .row = 1, .col = 1 },
        .visible_vars = new_var_set()
    };
    enum {
#define f(x, str) KEYWORD_##x,
    KEYWORDS(f)
#undef f
        KEYWORD_COUNT
    };
    parser.lexer.keywords = new_keywords_with_cap(KEYWORD_COUNT);
#define f(x, str) \
    insert_in_keywords(&parser.lexer.keywords, (struct keyword) { str, str + strlen(str) }, TOK_##x);
    KEYWORDS(f)
#undef f
    parser.ahead = lex(&parser.lexer);
    exp_t exp = parse_exp_internal(&parser);
    free_var_set(&parser.visible_vars);
    free_keywords(&parser.lexer.keywords);
    return exp;
}
