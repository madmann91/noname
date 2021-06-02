#include "ir/node.h"
#include "utils/buf.h"
#include "utils/hash.h"

MAP(var_map, label_t, node_t)

struct checker {
    mod_t mod;
    struct log* log;
    struct env {
        struct env* next, *prev;
        struct var_map vars;
    }* env;
};

static inline struct env* new_env(struct env* prev) {
    struct env* env = xmalloc(sizeof(struct env));
    env->prev = prev;
    env->next = NULL;
    env->vars = new_var_map();
    return env;
}

static inline void free_env(struct env* env) {
    free_var_map(&env->vars);
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
    clear_var_map(&env->vars);
}

static struct env* push_env(struct env* env) {
    if (!env->next)
        env->next = new_env(env);
    env = env->next;
    clear_env(env);
    return env;
}

static inline node_t find_in_env(struct env* env, label_t label) {
    while (env) {
        node_t* found = find_in_var_map(&env->vars, label);
        if (found)
            return *found;
        env = env->prev;
    }
    return NULL;
}

static inline void insert_in_env(struct env* env, node_t var) {
    assert(var->tag == NODE_VAR);
    insert_in_var_map(&env->vars, var->var.label, var);
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

static node_t infer_exp(struct checker* checker, node_t exp) {
    return check_exp(checker, exp, make_undef(checker->mod));
}

static node_t infer_annotated_var(struct checker* checker, node_t var) {
    assert(var->tag == NODE_VAR && var->type);
    node_t type = infer_exp(checker, var->type);
    return import_node(checker->mod, &(struct node) {
        .tag = NODE_VAR,
        .type = type,
        .var.label = var->var.label
    });
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

static inline node_t insert_var(struct checker* checker, node_t var) {
    checker->env = checker->env->next;
    insert_in_env(checker->env, var);
    checker->env = checker->env->prev;
    return var;
}

static node_t check_pat(struct checker* checker, node_t pat, node_t proto) {
    switch (pat->tag) {
        case NODE_VAR:
            return insert_var(checker, import_node(checker->mod, &(struct node) {
                .tag = NODE_VAR,
                .type = proto,
                .var.label = pat->var.label
            }));
        case NODE_LIT:
            return check_lit(checker, pat, proto);
        default:
            assert(false && "invalid pattern tag");
            return NULL;
    }
}

static struct node_pair check_binding(struct checker* checker, node_t pat, node_t exp) {
    switch (pat->tag) {
        case NODE_VAR: {
            if (pat->type) {
                pat = infer_annotated_var(checker, pat);
                exp = check_exp(checker, exp, pat->type);
            } else {
                exp = infer_exp(checker, exp);
                pat = import_node(checker->mod, &(struct node) {
                    .tag = NODE_VAR,
                    .type = exp->type,
                    .var.label = pat->var.label
                });
            }
            return (struct node_pair) { insert_var(checker, pat), exp };
        }
        default:
            assert(false && "invalid pattern tag");
            return (struct node_pair) { NULL, NULL };
    }
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

static inline node_t expect_type(struct checker* checker, node_t node, node_t proto, const struct loc* loc) {
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
        case NODE_UNI:   return expect_type(checker, make_uni(checker->mod), proto, &node->loc);
        case NODE_NAT:   return expect_type(checker, make_nat(checker->mod), proto, &node->loc);
        case NODE_INT:   return expect_type(checker, make_int(checker->mod), proto, &node->loc);
        case NODE_FLOAT: return expect_type(checker, make_float(checker->mod), proto, &node->loc);
        case NODE_STAR:  return expect_type(checker, make_star(checker->mod), proto, &node->loc);
        case NODE_LIT:   return check_lit(checker, node, proto);
        case NODE_APP:   return infer_app(checker, node);
        case NODE_VAR: {
            node_t var = find_in_env(checker->env, node->var.label);
            if (!var) {
                log_error(checker->log, &node->loc, "unknown identifier '%0:s'",
                    FORMAT_ARGS({ .s = node->var.label->name }));
                return make_untyped_err(checker->mod, &node->loc);
            }
            return var;
        }
        case NODE_MATCH: {
            node_t* pats = new_buf(node_t, node->match.pat_count);
            node_t* vals = new_buf(node_t, node->match.pat_count);
            node_t arg = infer_exp(checker, node->match.arg);
            checker->env = push_env(checker->env);
            checker->env = checker->env->prev;
            for (size_t i = 0, n = node->match.pat_count; i < n; ++i) {
                pats[i] = check_pat(checker, node->match.pats[i], arg->type);
                checker->env = checker->env->next;
                vals[i] = check_exp(checker, node->match.vals[i], proto);
                checker->env = checker->env->prev;
                proto = vals[i]->type;
            }
            node_t match = import_node(checker->mod, &(struct node) {
                .tag = NODE_MATCH,
                .type = proto,
                .loc = node->loc,
                .match = {
                    .arg = arg,
                    .pats = pats,
                    .vals = vals,
                    .pat_count = node->match.pat_count
                }
            });
            free_buf(pats);
            free_buf(vals);
            return match;
        }
        case NODE_LETREC:
        case NODE_LET: {
            node_t* vars = new_buf(node_t, node->let.var_count);
            node_t* vals = new_buf(node_t, node->let.var_count);
            checker->env = push_env(checker->env);
            if (node->tag == NODE_LET) {
                checker->env = checker->env->prev;
                for (size_t i = 0, n = node->let.var_count; i < n; ++i) {
                    struct node_pair pair = check_binding(checker, node->let.vars[i], node->let.vals[i]);
                    vars[i] = pair.fst;
                    vals[i] = pair.snd;
                }
                checker->env = checker->env->next;
            } else {
                // First put all variables in the environment
                for (size_t i = 0, n = node->letrec.var_count; i < n; ++i) {
                    assert(node->letrec.vars[i]->tag == NODE_VAR);
                    assert(node->letrec.vars[i]->type);
                    vars[i] = infer_annotated_var(checker, node->letrec.vars[i]);
                    insert_in_env(checker->env, vars[i]);
                }
                // Then check the values
                for (size_t i = 0, n = node->letrec.var_count; i < n; ++i)
                    vals[i] = check_exp(checker, node->letrec.vals[i], vars[i]->type);
            }
            node_t body = infer_exp(checker, node->let.body);
            checker->env = checker->env->prev;
            node_t type = reduce_and_replace_vars(checker, body->type);
            node_t let = import_node(checker->mod, &(struct node) {
                .tag = node->tag,
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
