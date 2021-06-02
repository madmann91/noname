#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "ir/node.h"
#include "utils/utf8.h"
#include "utils/lexer.h"
#include "utils/buf.h"
#include "utils/arena.h"
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
    f(EQ, "=")

#define KEYWORDS(f) \
    f(UNIVERSE, "Universe") \
    f(TYPE, "Type") \
    f(UINT, "UInt") \
    f(NAT, "Nat") \
    f(INT, "Int") \
    f(FLOAT, "Float") \
    f(IN, "in") \
    f(FUN, "fun") \
    f(LET, "let") \
    f(LETREC, "letrec") \
    f(MATCH, "match") \
    f(WITH, "with")

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
    mod_t mod;
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

static inline node_t make_node(struct parser* parser, const struct pos* begin, const struct node* node) {
    struct node* copy = alloc_from_arena(parser->arena, sizeof(struct node));
    memcpy(copy, node, sizeof(struct node));
    copy->loc.file = parser->lexer.file;
    copy->loc.begin = *begin;
    copy->loc.end = parser->prev_end;
    return copy;
}

static inline node_t* copy_and_free_nodes(struct parser* parser, struct node_vec* nodes) {
    node_t* copies = alloc_from_arena(parser->arena, sizeof(node_t) * nodes->size);
    memcpy(copies, nodes->elems, sizeof(node_t) * nodes->size);
    free_node_vec(nodes);
    return copies;
}

static inline label_t* copy_and_free_labels(struct parser* parser, struct label_vec* labels) {
    label_t* copies = alloc_from_arena(parser->arena, sizeof(label_t) * labels->size);
    memcpy(copies, labels->elems, sizeof(label_t) * labels->size);
    free_label_vec(labels);
    return copies;
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

static node_t parse_exp(struct parser*);
static node_t parse_pat(struct parser*);

static node_t parse_err(struct parser* parser, const char* msg) {
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
    return make_node(parser, &begin, &(struct node) { .tag = NODE_ERR });
}

static label_t parse_label(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    size_t len = parser->ahead->loc.end.ptr - parser->ahead->loc.begin.ptr;
    char* name = new_buf(char, len + 1);
    memcpy(name, parser->ahead->loc.begin.ptr, len);
    name[len] = 0;
    expect_tok(parser, TOK_IDENT);
    label_t label = make_label(parser->mod, name, &(struct loc) { .begin = begin, .end = parser->prev_end });
    free_buf(name);
    return label;
}

static node_t parse_var(struct parser* parser) {
    label_t label = parse_label(parser);
    return make_node(parser, &label->loc.begin, &(struct node) { .tag = NODE_VAR, .var.label = label });
}

static node_t parse_lit(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    struct lit lit = parser->ahead->lit;
    eat_tok(parser, TOK_LIT);
    return make_node(parser, &begin, &(struct node) { .tag = NODE_LIT, .lit = lit });
}

static node_t parse_paren(struct parser* parser, node_t (*parse_contents)(struct parser*)) {
    eat_tok(parser, TOK_LPAREN);
    node_t node = parse_contents(parser);
    expect_tok(parser, TOK_RPAREN);
    return node;
}

static node_t parse_annot(struct parser* parser, node_t node) {
    if (accept_tok(parser, TOK_COLON))
        ((struct node*)node)->type = parse_exp(parser);
    return node;
}

static node_t parse_let_or_letrec(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    bool is_rec = parser->ahead->tag == TOK_LETREC;
    eat_tok(parser, is_rec ? TOK_LETREC : TOK_LET);
    struct node_vec vars = new_node_vec();
    struct node_vec vals = new_node_vec();
    while (true) {
        node_t var = parse_annot(parser, parse_var(parser));
        if (is_rec && !var->type)
            log_error(parser->lexer.log, &var->loc, "recursive bindings must have a type annotation", NULL);
        push_to_node_vec(&vars, var);
        expect_tok(parser, TOK_EQ);
        push_to_node_vec(&vals, parse_exp(parser));
        if (!accept_tok(parser, TOK_COMMA))
            break;
    }
    size_t var_count = vars.size;
    expect_tok(parser, TOK_IN);
    node_t body = parse_exp(parser);
    return make_node(parser, &begin, &(struct node) {
        .tag = is_rec ? NODE_LETREC : NODE_LET,
        .let = {
            .vars = copy_and_free_nodes(parser, &vars),
            .vals = copy_and_free_nodes(parser, &vals),
            .var_count = var_count,
            .body = body
        }
    });
}

static node_t parse_fun(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    eat_tok(parser, TOK_FUN);
    node_t var = parse_annot(parser, parse_var(parser));
    expect_tok(parser, TOK_FATARROW);
    node_t body = parse_exp(parser);
    return make_node(parser, &begin, &(struct node) {
        .tag = NODE_FUN,
        .fun = {
            .var = var,
            .body = body
        }
    });
}

static node_t parse_match(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    eat_tok(parser, TOK_MATCH);
    node_t arg = parse_exp(parser);
    expect_tok(parser, TOK_WITH);
    accept_tok(parser, TOK_VBAR);
    struct node_vec pats = new_node_vec();
    struct node_vec vals = new_node_vec();
    while (true) {
        push_to_node_vec(&pats, parse_pat(parser));
        expect_tok(parser, TOK_FATARROW);
        push_to_node_vec(&vals, parse_exp(parser));
        if (!accept_tok(parser, TOK_VBAR))
            break;
    }
    return make_node(parser, &begin, &(struct node) {
        .tag = NODE_MATCH,
        .match = {
            .arg = arg,
            .pats = copy_and_free_nodes(parser, &pats),
            .vals = copy_and_free_nodes(parser, &vals),
            .pat_count = pats.size
        }
    });
}

static inline node_t parse_prod_or_record(struct parser* parser, unsigned sep, node_t (*parse_arg)(struct parser*)) {
    struct pos begin = parser->ahead->loc.begin;
    eat_tok(parser, TOK_LBRACE);
    struct node_vec args = new_node_vec();
    struct label_vec labels = new_label_vec();
    while (parser->ahead->tag == TOK_IDENT) {
        push_to_label_vec(&labels, parse_label(parser));
        expect_tok(parser, sep);
        push_to_node_vec(&args, parse_arg(parser));
        if (!accept_tok(parser, TOK_COMMA))
            break;
    }
    size_t arg_count = args.size;
    expect_tok(parser, TOK_RBRACE);
    return make_node(parser, &begin, &(struct node) {
        .tag = sep == TOK_COLON ? NODE_PROD : NODE_RECORD,
        .record = {
            .labels = copy_and_free_labels(parser, &labels),
            .args = copy_and_free_nodes(parser, &args),
            .arg_count = arg_count
        }
    });
}

static node_t parse_pat(struct parser* parser) {
    node_t pat = NULL;
    switch (parser->ahead->tag) {
        case TOK_IDENT:  pat = parse_var(parser); break;
        case TOK_LIT:    pat = parse_lit(parser); break;
        case TOK_LPAREN: pat = parse_paren(parser, parse_pat); break;
        case TOK_LBRACE: pat = parse_prod_or_record(parser, TOK_EQ, parse_pat); break;
        default:
            return parse_err(parser, "pattern");
    }
    return parse_annot(parser, pat);
}

static node_t make_basic_node(struct parser* parser, const struct pos* begin, unsigned tag) {
    eat_tok(parser, parser->ahead->tag);
    return make_node(parser, begin, &(struct node) { .tag = tag });
}

static node_t parse_basic_exp(struct parser* parser) {
    struct pos begin = parser->ahead->loc.begin;
    switch (parser->ahead->tag) {
        case TOK_UNIVERSE: return make_basic_node(parser, &begin, NODE_UNI);
        case TOK_TYPE:     return make_basic_node(parser, &begin, NODE_STAR);
        case TOK_NAT:      return make_basic_node(parser, &begin, NODE_NAT);
        case TOK_INT:      return make_basic_node(parser, &begin, NODE_INT);
        case TOK_FLOAT:    return make_basic_node(parser, &begin, NODE_FLOAT);
        case TOK_IDENT:    return parse_var(parser);
        case TOK_LIT:      return parse_lit(parser);
        case TOK_LPAREN:   return parse_paren(parser, parse_exp);
        case TOK_LBRACE:   return parse_prod_or_record(parser, parser->ahead[2].tag, parse_exp);
        case TOK_MATCH:    return parse_match(parser);
        case TOK_FUN:      return parse_fun(parser);
        case TOK_LET:
        case TOK_LETREC:
            return parse_let_or_letrec(parser);
        default:
            return parse_err(parser, "expression");
    }
}

static node_t parse_suffix_exp(struct parser* parser, node_t node) {
    struct pos begin = node->loc.begin;
    switch (parser->ahead->tag) {
        case TOK_THINARROW: {
            node_t var = make_node(parser, &begin, &(struct node) { .tag = NODE_VAR, .type = node });
            eat_tok(parser, TOK_THINARROW);
            node_t codom = parse_exp(parser);
            return make_node(parser, &begin, &(struct node) {
                .tag = NODE_ARROW,
                .arrow = {
                    .var = var,
                    .codom = codom
                }
            });
        }
        case TOK_DOT: {
            eat_tok(parser, TOK_DOT);
            if (parser->ahead->tag == TOK_LBRACE) {
                node_t record = parse_prod_or_record(parser, TOK_EQ, parse_exp);
                return make_node(parser, &begin, &(struct node) {
                    .tag = NODE_INS,
                    .ins = {
                        .val = node,
                        .record = record
                    }
                });
            } else {
                label_t label = parse_label(parser);
                return make_node(parser, &begin, &(struct node) {
                    .tag = NODE_EXT,
                    .ext = {
                        .val = node,
                        .label = label
                    }
                });
            }
        }
        case TOK_IDENT:
        case TOK_LIT:
        case TOK_NAT:
        case TOK_INT:
        case TOK_UNIVERSE:
        case TOK_TYPE:
        case TOK_FLOAT:
        case TOK_LPAREN:
        case TOK_LBRACE:
        case TOK_FUN:
        case TOK_MATCH:
        case TOK_LET:
        case TOK_LETREC: {
            node_t right = parse_basic_exp(parser);
            return make_node(parser, &begin, &(struct node) {
                .tag = NODE_APP,
                .app = { .left = node, .right = right }
            });
        }
        default:
            return node;
    }
}

static node_t parse_exp(struct parser* parser) {
    node_t cur = parse_basic_exp(parser), old;
    do {
        old = cur;
        cur = parse_suffix_exp(parser, old);
    } while (cur != old);
    return parse_annot(parser, cur);
}

node_t parse_node(mod_t mod, struct arena** arena, struct log* log, const char* file_name, const char* data, size_t data_size) {
    struct parser parser = {
        .mod = mod,
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
#define f(x, str) \
    { \
        static const char s[] = str; \
        insert_in_keywords(&parser.lexer.keywords, (struct keyword) { .begin = s, .end = s + strlen(s) }, TOK_##x); \
    }
    KEYWORDS(f)
#undef f
    for (int i = 0; i < MAX_AHEAD; ++i)
        parser.ahead[i] = lex(&parser.lexer);
    node_t node = parse_exp(&parser);
    free_keywords(&parser.lexer.keywords);
    return node;
}
