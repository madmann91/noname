#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "exp.h"
#include "utils.h"
#include "htable.h"
#include "arena.h"
#include "hash.h"

struct mod {
    struct htable exps;
    arena_t arena;
    exp_t uni, star, nat;
};

static bool cmp_exp(const void* ptr1, const void* ptr2) {
    exp_t exp1 = *(exp_t*)ptr1, exp2 = *(exp_t*)ptr2;
    if (exp1->tag != exp2->tag || exp1->type != exp2->type)
        return false;
    switch (exp1->tag) {
        case EXP_VAR:
            return exp1->var.index == exp2->var.index;
        case EXP_UNI:
            return exp1->uni.mod == exp2->uni.mod;
        case EXP_STAR:
        case EXP_NAT:
        case EXP_WILD:
        case EXP_TOP:
        case EXP_BOT:
            return true;
        case EXP_INT:
        case EXP_REAL:
            return exp1->real.bitwidth == exp2->real.bitwidth;
        case EXP_LIT:
            return exp1->type->tag == EXP_REAL
                ? exp1->lit.real_val == exp2->lit.real_val
                : exp1->lit.int_val  == exp2->lit.int_val;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            return
                exp1->tup.arg_count == exp2->tup.arg_count &&
                !memcmp(exp1->tup.args, exp2->tup.args, sizeof(exp_t) * exp1->tup.arg_count);
        case EXP_PI:
            return
                exp1->pi.dom == exp2->pi.dom &&
                exp1->pi.codom == exp2->pi.codom;
        case EXP_INJ:
            return
                exp1->inj.index == exp2->inj.index &&
                exp1->inj.arg == exp2->inj.arg;
        case EXP_ABS:
            return exp1->abs.body == exp2->abs.body;
        case EXP_APP:
            return
                exp1->app.left == exp2->app.left &&
                exp1->app.right == exp2->app.right;
        case EXP_LET:
        case EXP_LETREC:
            return
                exp1->let.body == exp2->let.body &&
                exp1->let.var_count == exp2->let.var_count &&
                !memcmp(exp1->let.vars, exp2->let.vars, sizeof(exp_t) * exp1->let.var_count) &&
                !memcmp(exp1->let.vals, exp2->let.vals, sizeof(exp_t) * exp1->let.var_count);
        case EXP_MATCH:
            return
                exp1->match.arg == exp2->match.arg &&
                exp1->match.pat_count == exp2->match.pat_count &&
                !memcmp(exp1->match.vals, exp2->match.vals, sizeof(exp_t) * exp1->match.pat_count) &&
                !memcmp(exp1->match.pats, exp2->match.pats, sizeof(exp_t) * exp1->match.pat_count);
        default:
            assert(false && "invalid expression tag");
            return false;
    }
}

static inline uint32_t hash_exp(exp_t exp) {
    uint32_t hash = FNV_OFFSET;
    hash = hash_uint(hash, exp->tag);
    hash = hash_ptr(hash, exp->type);
    switch (exp->tag) {
        case EXP_VAR:
            hash = hash_uint(hash, exp->var.index);
            break;
        case EXP_UNI:
            hash = hash_ptr(hash, exp->uni.mod);
            break;
        default:
            assert(false && "invalid expression tag");
            // fallthrough
        case EXP_STAR:
        case EXP_NAT:
        case EXP_WILD:
        case EXP_TOP:
        case EXP_BOT:
            break;
        case EXP_INT:
        case EXP_REAL:
            hash = hash_ptr(hash, exp->real.bitwidth);
            break;
        case EXP_LIT:
            hash = exp->type->tag == EXP_REAL
                ? hash_bytes(hash, &exp->lit.real_val, sizeof(exp->lit.real_val))
                : hash_uint(hash, exp->lit.int_val);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                hash = hash_ptr(hash, exp->tup.args[i]);
            break;
        case EXP_PI:
            hash = hash_ptr(hash, exp->pi.dom);
            hash = hash_ptr(hash, exp->pi.codom);
            break;
        case EXP_INJ:
            hash = hash_uint(hash, exp->inj.index);
            hash = hash_ptr(hash, exp->inj.arg);
            break;
        case EXP_ABS:
            hash = hash_ptr(hash, exp->abs.body);
            break;
        case EXP_APP:
            hash = hash_ptr(hash, exp->app.left);
            hash = hash_ptr(hash, exp->app.right);
            break;
        case EXP_LET:
        case EXP_LETREC:
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                hash = hash_ptr(hash, exp->let.vars[i]);
                hash = hash_ptr(hash, exp->let.vals[i]);
            }
            hash = hash_ptr(hash, exp->let.body);
            break;
        case EXP_MATCH:
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                hash = hash_ptr(hash, exp->match.pats[i]);
                hash = hash_ptr(hash, exp->match.vals[i]);
            }
            hash = hash_ptr(hash, exp->match.arg);
            break;
    }
    return hash;
}

static inline exp_t* copy_exps(mod_t mod, const exp_t* exps, size_t count) {
    exp_t* new_exps = alloc_from_arena(&mod->arena, sizeof(exp_t) * count);
    memcpy(new_exps, exps, sizeof(exp_t) * count);
    return new_exps;
}

static inline size_t max_depth(exp_t exp1, exp_t exp2) {
    return exp1->depth > exp2->depth ? exp1->depth : exp2->depth;
}

static inline exp_t insert_exp(mod_t mod, exp_t exp) {
    uint32_t hash = hash_exp(exp);
    exp_t* found = find_in_htable(&mod->exps, &exp, hash);
    if (found)
        return *found;

    struct exp* new_exp = alloc_from_arena(&mod->arena, sizeof(struct exp));
    memcpy(new_exp, exp, sizeof(struct exp));
    new_exp->depth = 0;

    // Copy the data contained in the original expression and set the depth
    switch (exp->tag) {
        case EXP_WILD:
            new_exp->depth = max_depth(new_exp, exp->wild.sub_pat);
            break;
        case EXP_INT:
        case EXP_REAL:
            new_exp->depth = max_depth(new_exp, exp->real.bitwidth);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                new_exp->depth = max_depth(new_exp, exp->tup.args[i]);
            new_exp->tup.args = copy_exps(mod, exp->tup.args, exp->tup.arg_count);
            break;
        case EXP_INJ:
            new_exp->depth = max_depth(new_exp, exp->inj.arg);
            break;
        case EXP_PI:
            new_exp->depth = max_depth(new_exp, exp->pi.dom);
            new_exp->depth = max_depth(new_exp, exp->pi.codom);
            new_exp->depth++;
            break;
        case EXP_ABS:
            new_exp->depth = max_depth(new_exp, exp->abs.body) + 1;
            break;
        case EXP_APP:
            new_exp->depth = max_depth(new_exp, exp->app.left);
            new_exp->depth = max_depth(new_exp, exp->app.right);
            break;
        case EXP_LET:
        case EXP_LETREC:
            new_exp->depth = max_depth(new_exp, exp->let.body);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i)
                new_exp->depth = max_depth(new_exp, exp->let.vals[i]);
            new_exp->let.vars = copy_exps(mod, exp->let.vars, exp->let.var_count);
            new_exp->let.vals = copy_exps(mod, exp->let.vals, exp->let.var_count);
            new_exp->depth += exp->let.var_count;
            break;
        case EXP_MATCH:
            new_exp->match.vals = copy_exps(mod, exp->match.vals, exp->match.pat_count);
            new_exp->match.pats = copy_exps(mod, exp->match.pats, exp->match.pat_count);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i)
                new_exp->depth = max_depth(new_exp, exp->match.vals[i]);
            new_exp->depth += exp->match.pat_count;
            break;
        default:
            assert(false);
        case EXP_VAR:
        case EXP_UNI:
        case EXP_STAR:
        case EXP_NAT:
        case EXP_TOP:
        case EXP_BOT:
        case EXP_LIT:
            break;
    }

    exp_t copy = new_exp;
    bool ok = insert_in_htable(&mod->exps, &copy, hash, NULL);
    assert(ok); (void)ok;
    return new_exp;
}

mod_t new_mod(void) {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->exps = new_htable(sizeof(exp_t), DEFAULT_CAP, cmp_exp);
    mod->arena = new_arena(DEFAULT_ARENA_SIZE);
    mod->uni  = insert_exp(mod, &(struct exp) { .tag = EXP_UNI,  .uni.mod = mod });
    mod->star = insert_exp(mod, &(struct exp) { .tag = EXP_STAR, .type = mod->uni });
    mod->nat  = insert_exp(mod, &(struct exp) { .tag = EXP_NAT,  .type = mod->star });
    return mod;
}

void free_mod(mod_t mod) {
    free_htable(&mod->exps);
    free_arena(mod->arena);
    free(mod);
}

mod_t get_mod(exp_t exp) {
    while (exp->tag != EXP_UNI)
        exp = exp->type;
    return exp->uni.mod;
}

bool is_pat(exp_t exp) {
    // TODO: Check that the expression is a valid pattern
    return true;
}

exp_t new_var(mod_t mod, exp_t type, size_t index, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_VAR,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .var.index = index
    });
}

exp_t new_uni(mod_t mod) {
    return mod->uni;
}

exp_t new_star(mod_t mod) {
    return mod->star;
}

exp_t new_nat(mod_t mod) {
    return mod->nat;
}

exp_t new_wild(mod_t mod, exp_t type, exp_t sub_pat, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_WILD,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .wild.sub_pat = sub_pat
    });
}

exp_t new_top(mod_t mod, exp_t type, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_TOP,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
    });
}

exp_t new_bot(mod_t mod, exp_t type, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_BOT,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
    });
}

exp_t new_int(mod_t mod, exp_t bitwidth, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_INT,
        .type = new_star(mod),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .int_.bitwidth = bitwidth
    });
}

exp_t new_real(mod_t mod, exp_t bitwidth, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_REAL,
        .type = new_star(mod),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .int_.bitwidth = bitwidth
    });
}

exp_t new_lit(mod_t mod, exp_t type, const union lit* lit, const struct loc* loc) {
    assert(type->tag == EXP_INT || type->tag == EXP_REAL || type->tag == EXP_NAT);
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_LIT,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .lit = *lit
    });
}

exp_t new_sum(mod_t mod, exp_t* args, size_t arg_count, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_SUM,
        .type = new_star(mod),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .sum = {
            .args = args,
            .arg_count = arg_count
        }
    });
}

exp_t new_prod(mod_t mod, exp_t* args, size_t arg_count, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_PROD,
        .type = new_star(mod),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .prod = {
            .args = args,
            .arg_count = arg_count
        }
    });
}

exp_t new_pi(mod_t mod, exp_t var, exp_t dom, exp_t codom, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_PI,
        .type = codom->type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .pi = {
            .var = var,
            .dom = dom,
            .codom = codom
        }
    });
}

exp_t new_inj(mod_t mod, exp_t type, size_t index, exp_t arg, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_INJ,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .inj = {
            .index = index,
            .arg = arg
        }
    });
}

exp_t new_tup(mod_t mod, exp_t* args, size_t arg_count, const struct loc* loc) {
    NEW_BUF(prod_args, exp_t, arg_count)
    for (size_t i = 0; i < arg_count; ++i)
        prod_args[i] = args[i]->type;
    exp_t type = new_prod(mod, prod_args, arg_count, loc);
    FREE_BUF(prod_args);
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_TUP,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .tup = {
            .args = args,
            .arg_count = arg_count
        }
    });
}

static inline exp_t infer_abs_type(exp_t var, exp_t body) {
    // TODO: Check that the body does depend on var, and
    // if it does not, use NULL as variable.
    return new_pi(get_mod(var), var, var->type, body->type, NULL);
}

exp_t new_abs(mod_t mod, exp_t var, exp_t body, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_ABS,
        .type = infer_abs_type(var, body),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .abs.body = body
    });
}

exp_t new_app(mod_t mod, exp_t left, exp_t right, const struct loc* loc) {
    assert(left->type->tag == EXP_PI);
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_ABS,
        .type = left->type->pi.var
            ? replace_exp(left->type->pi.codom, left->type->pi.var, right)
            : left->type->pi.codom,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .app = {
            .left = left,
            .right = right
        }
    });
}

static inline exp_t infer_let_type(exp_t* vars, exp_t* vals, size_t var_count, exp_t body_type) {
    // Replace bound variables in the expression and reduce it
    // until a fix point is reached. This may loop forever if
    // the expression does not terminate.
    bool todo;
    do {
        exp_t old_type = body_type;
        for (size_t i = 0; i < var_count; ++i)
            body_type = replace_exp(body_type, vars[i], vals[i]);
        body_type = reduce_exp(body_type);
        todo = old_type != body_type;
    } while (todo);
    return body_type;
}

exp_t new_let(mod_t mod, exp_t* vars, exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_LET,
        .type = infer_let_type(vars, vals, var_count, body->type),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .let = {
            .vars = vars,
            .vals = vals,
            .var_count = var_count,
            .body = body
        }
    });
}

exp_t new_letrec(mod_t mod, exp_t* vars, exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_LETREC,
        .type = infer_let_type(vars, vals, var_count, body->type),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .letrec = {
            .vars = vars,
            .vals = vals,
            .var_count = var_count,
            .body = body
        }
    });
}

exp_t new_match(mod_t mod, exp_t* pats, exp_t* vals, size_t pat_count, exp_t arg, const struct loc* loc) {
    assert(pat_count > 0);
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_MATCH,
        .type = vals[0]->type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .match = {
            .arg = arg,
            .pats = pats,
            .vals = vals
        }
    });
}

exp_t rebuild_exp(exp_t exp) {
    return import_exp(get_mod(exp), exp);
}

exp_t import_exp(mod_t mod, exp_t exp) {
    switch (exp->tag) {
        case EXP_VAR:    return new_var(mod, exp->type, exp->var.index, &exp->loc);
        case EXP_UNI:    return new_uni(mod);
        case EXP_STAR:   return new_star(mod);
        case EXP_NAT:    return new_nat(mod);
        case EXP_WILD:   return new_wild(mod, exp->type, exp->wild.sub_pat, &exp->loc);
        case EXP_TOP:    return new_top(mod, exp->type, &exp->loc);
        case EXP_BOT:    return new_bot(mod, exp->type, &exp->loc);
        case EXP_INT:    return new_int(mod, exp->int_.bitwidth, &exp->loc);
        case EXP_REAL:   return new_real(mod, exp->real.bitwidth, &exp->loc);
        case EXP_LIT:    return new_lit(mod, exp->type, &exp->lit, &exp->loc);
        case EXP_SUM:    return new_sum(mod, exp->sum.args, exp->sum.arg_count, &exp->loc);
        case EXP_PROD:   return new_prod(mod, exp->prod.args, exp->prod.arg_count, &exp->loc);
        case EXP_PI:     return new_pi(mod, exp->pi.var, exp->pi.dom, exp->pi.codom, &exp->loc);
        case EXP_INJ:    return new_inj(mod, exp->type, exp->inj.index, exp->inj.arg, &exp->loc);
        case EXP_TUP:    return new_tup(mod, exp->tup.args, exp->tup.arg_count, &exp->loc);
        case EXP_ABS:    return new_abs(mod, exp->abs.var, exp->abs.body, &exp->loc);
        case EXP_APP:    return new_app(mod, exp->app.left, exp->app.right, &exp->loc);
        case EXP_LET:    return new_let(mod, exp->let.vars, exp->let.vals, exp->let.var_count, exp->let.body, &exp->loc);
        case EXP_LETREC: return new_letrec(mod, exp->letrec.vars, exp->letrec.vals, exp->letrec.var_count, exp->letrec.body, &exp->loc);
        case EXP_MATCH:  return new_match(mod, exp->match.pats, exp->match.vals, exp->match.pat_count, exp->match.arg, &exp->loc);
        default:
            assert(false);
            break;
    }
}

exp_t replace_exp(exp_t exp, exp_t from, exp_t to) {
    // TODO: Implement replacement
    return exp;
}

exp_t reduce_exp(exp_t exp) {
    // TODO: Implement reduction
    return exp;
}
