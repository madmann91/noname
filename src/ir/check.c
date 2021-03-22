#include "ir/node.h"
#include "utils/buf.h"

struct checker {
    mod_t mod;
    struct log* log;
    struct env {
        struct env* next, *prev;
        struct label_vec labels;
        struct node_vec vars;
    }* env;
};

static inline struct env* new_env(struct env* prev) {
    struct env* env = xmalloc(sizeof(struct env));
    env->prev = prev;
    env->next = NULL;
    env->labels = new_label_vec();
    env->vars = new_node_vec();
    return env;
}

static inline void free_env(struct env* env) {
    free_node_vec(&env->vars);
    free_label_vec(&env->labels);
    free(env);
}

static inline void free_envs(struct env* env) {
    while (env->next)
        env = env->next;
    while (env) {
        struct env* prev = env->prev;
        free_env(env);
        env = prev;
    }
}

static inline void clear_env(struct env* env) {
    clear_node_vec(&env->vars);
    clear_label_vec(&env->labels);
}

static struct env* push_env(struct env* env) {
    if (!env->next)
        env->next = new_env(env);
    env = env->next;
    clear_env(env);
    return env;
}

static inline node_t find_in_env(struct env* env, label_t label) {
    assert(false && "TODO");
    return NULL;
}

static inline void insert_in_env(struct env* env, label_t label, node_t node) {
    assert(false && "TODO");
}

static inline label_t find_non_shadowing_label(mod_t mod, struct env* env, label_t label) {
    // TODO
    return label;
}

static inline node_t reduce_and_replace_vars(struct checker* checker, node_t node) {
    // TODO
    return node;
}

static inline void invalid_type(struct checker* checker, node_t type, const char* msg, const struct loc* loc) {
    if (!has_err(type)) {
        log_error(checker->log, loc, "invalid type '%0:n' for %1:s",
            FORMAT_ARGS({ .n = type }, { .s = msg }));
    }
}

static inline node_t match_type(struct checker* checker, node_t from, node_t to, const struct loc* loc) {
    if (from == to || to->tag == NODE_UNDEF) return from;
    if (from->tag == NODE_UNDEF) return to;
    // TODO: Tuples, ...
    log_error(checker->log, loc, "expected type '%0:n', but got '%1:n'",
        FORMAT_ARGS({ .n = to }, { .n = from }));
    return make_untyped_err(checker->mod, loc);
}

struct node_pair {
    node_t fst, snd;
};

static node_t check_exp(struct checker*, node_t, node_t);
static struct node_pair check_pat(struct checker*, node_t, node_t);

static node_t infer_exp(struct checker* checker, node_t exp) {
    return check_exp(checker, exp, make_undef(checker->mod));
}

static struct node_pair check_pat(struct checker* checker, node_t pat, node_t exp) {
    switch (pat->tag) {
        case NODE_VAR: {
            node_t type = NULL;
            if (pat->type) {
                type = infer_exp(checker, pat->type);
                exp  = check_exp(checker, exp, type);
            } else {
                exp = infer_exp(checker, exp);
                type = exp->type;
            }
            pat = import_node(checker->mod, &(struct node) {
                .tag = NODE_VAR,
                .type = type,
                .var.label = pat->var.label
            });
            return (struct node_pair) { pat, exp };
        }
        default:
            assert(false && "invalid pattern tag");
            return (struct node_pair) { NULL, NULL };
    }
}

static inline node_t check_lit(struct checker* checker, node_t node, node_t proto) {
    assert(node->tag == NODE_LIT);
    node_t type = NULL;
    if (proto->tag == NODE_UNDEF) {
        type = node->lit.tag == LIT_FLOAT
            ? make_float_app(checker->mod, make_nat_lit(checker->mod, 64, NULL), NULL)
            : make_nat(checker->mod);
    } else if (proto->tag == NODE_NAT || is_int_or_float_app(proto)) {
        type = proto;
    } else {
        invalid_type(checker, proto,
            node->lit.tag == LIT_INT ? "integer literal" : "floating-point literal", &node->loc);
        type = make_untyped_err(checker->mod, &node->loc);
    }
    return import_node(checker->mod, &(struct node) {
        .tag = NODE_LIT,
        .type = type,
        .loc = node->loc,
        .lit = node->lit
    });
}

static node_t infer_app(struct checker* checker, node_t node) {
    assert(node->tag == NODE_APP);
    node_t left = infer_exp(checker, node->app.left);
    node_t right = left->type->tag == NODE_ARROW
        ? check_exp(checker, node->app.right, left->type->arrow.var->type)
        : infer_exp(checker, node->app.right);
    if (left->type->tag != NODE_ARROW)
        invalid_type(checker, left->type, "application callee", &node->app.left->loc);
    node_t type = replace_var(left->type->arrow.codom, left->type->arrow.var, right->type);
    return import_node(checker->mod, &(struct node) {
        .tag  = NODE_APP,
        .type = type,
        .loc  = node->loc,
        .app  = { .left = left, .right = right }
    });
}

static inline node_t make_and_match(struct checker* checker, node_t node, node_t proto, const struct loc* loc) {
    if (node->type)
        match_type(checker, node->type, proto, loc);
    else if (proto->tag != NODE_UNDEF) {
        log_error(checker->log, loc, "expected type '%0:n', but the node '%1:n' has no type",
            FORMAT_ARGS({ .n = proto }, { .n = node }));
    }
    return node;
}

static node_t check_exp(struct checker* checker, node_t node, node_t proto) {
    if (node->type) {
        // If the node is annotated with a type, use that,
        // but make sure it's compatible with the prototype.
        proto = match_type(checker, infer_exp(checker, node->type), proto, &node->loc);
    }

    switch (node->tag) {
        case NODE_UNI:   return make_and_match(checker, make_uni(checker->mod), proto, &node->loc);
        case NODE_NAT:   return make_and_match(checker, make_nat(checker->mod), proto, &node->loc);
        case NODE_INT:   return make_and_match(checker, make_int(checker->mod), proto, &node->loc);
        case NODE_FLOAT: return make_and_match(checker, make_float(checker->mod), proto, &node->loc);
        case NODE_STAR:  return make_and_match(checker, make_star(checker->mod), proto, &node->loc);
        case NODE_VAR:   return find_in_env(checker->env, node->var.label);
        case NODE_LIT:   return check_lit(checker, node, proto);
        case NODE_APP:   return infer_app(checker, node);
        case NODE_LET: {
            node_t* vars = new_buf(node_t, node->let.var_count);
            node_t* vals = new_buf(node_t, node->let.var_count);
            for (size_t i = 0, n = node->let.var_count; i < n; ++i) {
                struct node_pair pair = check_pat(checker, node->let.vars[i], node->let.vals[i]);
                vars[i] = pair.fst;
                vals[i] = pair.snd;
            }
            node_t body = infer_exp(checker, node->let.body);
            node_t type = reduce_and_replace_vars(checker, body->type);
            node_t let = import_node(checker->mod, &(struct node) {
                .tag = NODE_LET,
                .type = type,
                .loc = node->loc,
                .let = {
                    .vars = vars,
                    .vals = vals,
                    .var_count = node->let.var_count,
                    .body = body
                }
            });
            free_buf(vars);
            free_buf(vals);
            return let;
        }
        default:
            assert(false && "invalid node tag");
            return NULL;
    }
}

node_t check_node(mod_t mod, struct log* log, node_t node) { 
    struct checker checker = {
        .mod = mod,
        .log = log,
        .env = new_env(NULL)
    };
    node = infer_exp(&checker, node);
    free_envs(checker.env);
    return node;
}
