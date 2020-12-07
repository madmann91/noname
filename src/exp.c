#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/arena.h"
#include "utils/hash.h"
#include "utils/format.h"
#include "utils/vec.h"
#include "utils/buf.h"
#include "utils/sort.h"
#include "exp.h"

// Hash consing --------------------------------------------------------------------

static inline bool compare_vars(const void*, const void*);
static inline bool compare_exp(const void*, const void*);
static inline uint32_t hash_vars(const void*);
static inline uint32_t hash_exp(const void*);
CUSTOM_MAP(mod_exps, exp_t, exp_t, hash_exp, compare_exp)
CUSTOM_SET(mod_vars, vars_t, hash_vars, compare_vars)

// Module --------------------------------------------------------------------------

struct mod {
    arena_t arena;
    struct mod_exps exps;
    struct mod_vars vars;
    exp_t uni, star, nat;
    struct log* log;
};

// Free variables ------------------------------------------------------------------

static inline bool compare_vars(const void* ptr1, const void* ptr2) {
    vars_t vars1 = *(vars_t*)ptr1, vars2 = *(vars_t*)ptr2;
    return
        vars1->count == vars2->count &&
        !memcmp(vars1->vars, vars2->vars, sizeof(exp_t) * vars1->count);
}

static inline uint32_t hash_vars(const void* ptr) {
    vars_t vars = *(vars_t*)ptr;
    uint32_t h = FNV_OFFSET;
    for (size_t i = 0, n = vars->count; i < n; ++i)
        h = hash_ptr(h, vars->vars[i]);
    return h;
}

static inline vars_t insert_vars(mod_t mod, vars_t vars) {
    const vars_t* found = find_in_mod_vars(&mod->vars, vars);
    if (found)
        return *found;

    struct vars* new_vars = alloc_from_arena(&mod->arena, sizeof(struct vars));
    new_vars->vars = alloc_from_arena(&mod->arena, sizeof(exp_t) * vars->count);
    new_vars->count = vars->count;
    memcpy((exp_t*)new_vars->vars, vars->vars, sizeof(exp_t) * vars->count);
    vars_t copy = new_vars;
    insert_in_mod_vars(&mod->vars, copy);
    return new_vars;
}

SORT(sort_vars, exp_t)

vars_t new_vars(mod_t mod, const exp_t* vars, size_t count) {
    exp_t* sorted_vars = new_buf(exp_t, count);
    memcpy(sorted_vars, vars, sizeof(exp_t) * count);
    sort_vars(sorted_vars, count);
#ifndef NDEBUG
    for (size_t i = 1; i < count; ++i)
        assert(sorted_vars[i - 1] < sorted_vars[i]);
#endif
    vars_t res = insert_vars(mod, &(struct vars) { .vars = sorted_vars, .count = count });
    free_buf(sorted_vars);
    return res;
}

vars_t union_vars(mod_t mod, vars_t vars1, vars_t vars2) {
    exp_t* vars = new_buf(exp_t, vars1->count + vars2->count);
    size_t i = 0, j = 0, count = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            vars[count++] = vars1->vars[i++];
        else if (vars1->vars[i] > vars2->vars[j])
            vars[count++] = vars2->vars[j++];
        else
            vars[count++] = vars1->vars[i++], j++;
    }
    while (i < vars1->count) vars[count++] = vars1->vars[i++];
    while (j < vars2->count) vars[count++] = vars2->vars[j++];
    vars_t res = new_vars(mod, vars, count);
    free_buf(vars);
    return res;
}

vars_t intr_vars(mod_t mod, vars_t vars1, vars_t vars2) {
    size_t min_count = vars1->count < vars2->count ? vars1->count : vars2->count;
    exp_t* vars = new_buf(exp_t, min_count);
    size_t i = 0, j = 0, count = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            i++;
        else if (vars1->vars[i] > vars2->vars[j])
            j++;
        else
            vars[count++] = vars1->vars[i++], j++;
    }
    vars_t res = new_vars(mod, vars, count);
    free_buf(vars);
    return res;
}

vars_t diff_vars(mod_t mod, vars_t vars1, vars_t vars2) {
    exp_t* vars = new_buf(exp_t, vars1->count);
    size_t i = 0, j = 0, count = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            vars[count++] = vars1->vars[i++];
        else if (vars1->vars[i] > vars2->vars[j])
            j++;
        else
            i++, j++;
    }
    while (i < vars1->count) vars[count++] = vars1->vars[i++];
    vars_t res = new_vars(mod, vars, count);
    free_buf(vars);
    return res;
}

bool contains_vars(vars_t vars1, vars_t vars2) {
    size_t i = 0, j = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            i++;
        else if (vars1->vars[i] > vars2->vars[j])
            j++;
        else
            return true;
    }
    return false;
}

bool contains_var(vars_t vars, exp_t var) {
    assert(var->tag == EXP_VAR);
    if (vars->count == 0)
        return false;
    size_t i = 0, j = vars->count - 1;
    while (i <= j) {
        size_t m = (i + j) / 2;
        if (vars->vars[m] < var)
            i = m + 1;
        else if (vars->vars[m] > var) {
            if (m == 0) return false;
            j = m - 1;
        } else
            return true;
    }
    return false;
}

// Expressions ---------------------------------------------------------------------

static inline bool compare_exp(const void* ptr1, const void* ptr2) {
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
        case EXP_INS:
            if (exp1->ins.elem != exp2->ins.elem)
                return false;
            // fallthrough
        case EXP_EXT:
            return
                exp1->ext.val == exp2->ext.val &&
                exp1->ext.index == exp2->ext.index;
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

static inline uint32_t hash_exp(const void* ptr) {
    exp_t exp = *(exp_t*)ptr;
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
        case EXP_INS:
            hash = hash_ptr(hash, exp->ins.elem);
            // fallthrough
        case EXP_EXT:
            hash = hash_ptr(hash, exp->ext.val);
            hash = hash_ptr(hash, exp->ext.index);
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

exp_t simplify_exp(mod_t, exp_t);

static inline exp_t insert_exp(mod_t mod, exp_t exp) {
    exp_t* found = find_in_mod_exps(&mod->exps, exp);
    if (found)
        return *found;

    struct exp* new_exp = alloc_from_arena(&mod->arena, sizeof(struct exp));
    memcpy(new_exp, exp, sizeof(struct exp));
    new_exp->free_vars = exp->type ? exp->type->free_vars : new_vars(mod, NULL, 0);
    new_exp->depth = 0;

    // Copy the data contained in the original expression and compute properties
    switch (exp->tag) {
        case EXP_INT:
        case EXP_REAL:
            new_exp->depth = max_depth(new_exp, exp->real.bitwidth);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->real.bitwidth->free_vars);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                new_exp->depth = max_depth(new_exp, exp->tup.args[i]);
                new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->tup.args[i]->free_vars);
            }
            new_exp->tup.args = copy_exps(mod, exp->tup.args, exp->tup.arg_count);
            break;
        case EXP_INJ:
            new_exp->depth = max_depth(new_exp, exp->inj.arg);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->inj.arg->free_vars);
            break;
        case EXP_INS:
            new_exp->depth = max_depth(new_exp, exp->ins.elem);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->ins.elem->free_vars);
            // fallthrough
        case EXP_EXT:
            new_exp->depth = max_depth(new_exp, exp->ext.val);
            new_exp->depth = max_depth(new_exp, exp->ext.index);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->ext.val->free_vars);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->ext.index->free_vars);
            break;
        case EXP_PI:
            new_exp->depth = max_depth(new_exp, exp->pi.dom);
            new_exp->depth = max_depth(new_exp, exp->pi.codom);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->pi.dom->free_vars);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->pi.codom->free_vars);
            if (exp->pi.var)
                new_exp->free_vars = diff_vars(mod, new_exp->free_vars, new_vars(mod, &exp->pi.var, 1));
            new_exp->depth++;
            break;
        case EXP_ABS:
            new_exp->depth = max_depth(new_exp, exp->abs.body) + 1;
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->abs.body->free_vars);
            new_exp->free_vars = diff_vars(mod, new_exp->free_vars, new_vars(mod, &exp->abs.var, 1));
            break;
        case EXP_APP:
            new_exp->depth = max_depth(new_exp, exp->app.left);
            new_exp->depth = max_depth(new_exp, exp->app.right);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->app.left->free_vars);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->app.right->free_vars);
            break;
        case EXP_LET:
        case EXP_LETREC:
            new_exp->depth = max_depth(new_exp, exp->let.body);
            new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->let.body->free_vars);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                new_exp->depth = max_depth(new_exp, exp->let.vals[i]);
                new_exp->free_vars = union_vars(mod, new_exp->free_vars, exp->let.vals[i]->free_vars);
            }
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i)
                new_exp->free_vars = diff_vars(mod, new_exp->free_vars, new_vars(mod, &exp->let.vars[i], 1));
            new_exp->let.vars = copy_exps(mod, exp->let.vars, exp->let.var_count);
            new_exp->let.vals = copy_exps(mod, exp->let.vals, exp->let.var_count);
            new_exp->depth += exp->let.var_count;
            break;
        case EXP_MATCH:
            new_exp->match.vals = copy_exps(mod, exp->match.vals, exp->match.pat_count);
            new_exp->match.pats = copy_exps(mod, exp->match.pats, exp->match.pat_count);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                new_exp->depth = max_depth(new_exp, exp->match.vals[i]);
                new_exp->free_vars = union_vars(mod, new_exp->free_vars,
                    diff_vars(mod, exp->match.vals[i]->free_vars, exp->match.pats[i]->free_vars));
            }
            new_exp->depth += exp->match.pat_count;
            break;
        case EXP_VAR:
            new_exp->free_vars = new_vars(mod, (const exp_t*)&new_exp, 1);
            break;
        default:
            assert(false);
            // fallthrough
        case EXP_UNI:
        case EXP_STAR:
        case EXP_NAT:
        case EXP_WILD:
        case EXP_TOP:
        case EXP_BOT:
        case EXP_LIT:
            break;
    }

    exp_t res = simplify_exp(mod, new_exp);
    bool ok = insert_in_mod_exps(&mod->exps, new_exp, res);
    assert(ok); (void)ok;
    return res;
}

// Module --------------------------------------------------------------------------

mod_t new_mod(struct log* log) {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->arena = new_arena(DEFAULT_ARENA_SIZE);
    mod->exps = new_mod_exps();
    mod->vars = new_mod_vars();
    mod->uni  = insert_exp(mod, &(struct exp) { .tag = EXP_UNI,  .uni.mod = mod });
    mod->star = insert_exp(mod, &(struct exp) { .tag = EXP_STAR, .type = mod->uni });
    mod->nat  = insert_exp(mod, &(struct exp) { .tag = EXP_NAT,  .type = mod->star });
    mod->log = log;
    return mod;
}

void free_mod(mod_t mod) {
    free_mod_exps(&mod->exps);
    free_mod_vars(&mod->vars);
    free_arena(mod->arena);
    free(mod);
}

mod_t get_mod(exp_t exp) {
    while (exp->tag != EXP_UNI)
        exp = exp->type;
    return exp->uni.mod;
}

// Patterns ------------------------------------------------------------------------

bool is_pat(exp_t exp) {
    switch (exp->tag) {
        case EXP_WILD: return true;
        case EXP_LIT:  return true;
        case EXP_VAR:  return true;
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                if (!is_pat(exp->tup.args[i]))
                    return false;
            }
            return true;
        case EXP_INJ:
            return is_pat(exp->inj.arg);
        default:
            return false;
    }
}

bool is_trivial_pat(exp_t exp) {
    switch (exp->tag) {
        case EXP_WILD: return true;
        case EXP_LIT:  return false;
        case EXP_VAR:  return true;
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                if (!is_trivial_pat(exp->tup.args[i]))
                    return false;
            }
            return true;
        default:
            assert(false && "invalid pattern");
            // fallthrough
        case EXP_INJ:
            return false;
    }
}

vars_t collect_bound_vars(exp_t pat) {
    mod_t mod = get_mod(pat);
    switch (pat->tag) {
        default:
            assert(false && "invalid pattern");
            // fallthrough
        case EXP_WILD: 
        case EXP_LIT:
            return new_vars(mod, NULL, 0);
        case EXP_VAR:
            return new_vars(mod, &pat, 1);
        case EXP_TUP: {
            vars_t vars = new_vars(mod, NULL, 0);
            for (size_t i = 0, n = pat->tup.arg_count; i < n; ++i)
                vars = union_vars(mod, vars, collect_bound_vars(pat->tup.args[i]));
            return vars;
        }
        case EXP_INJ:
            return collect_bound_vars(pat->inj.arg);
    }
}

// Constructors --------------------------------------------------------------------

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

exp_t new_wild(mod_t mod, exp_t type, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_WILD,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL }
    });
}

exp_t new_top(mod_t mod, exp_t type, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_TOP,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL }
    });
}

exp_t new_bot(mod_t mod, exp_t type, const struct loc* loc) {
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_BOT,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL }
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
    if (mod->log &&
        type->tag != EXP_INT &&
        type->tag != EXP_REAL &&
        type->tag != EXP_NAT) {
        log_error(mod->log, loc, "invalid type '%0:e' for literal", FMT_ARGS({ .e = type }));
        return NULL;
    }
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_LIT,
        .type = type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .lit = *lit
    });
}

exp_t new_sum(mod_t mod, const exp_t* args, size_t arg_count, const struct loc* loc) {
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

exp_t new_prod(mod_t mod, const exp_t* args, size_t arg_count, const struct loc* loc) {
    if (mod->log) {
        for (size_t i = 0; i < arg_count; ++i) {
            if (args[i]->type->tag != EXP_STAR) {
                log_error(mod->log, &args[i]->loc,
                    "invalid type '%0:e' for product type argument",
                    FMT_ARGS({ .e = args[i]->type }));
                return NULL;
            }
        }
    }
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
    if (mod->log) {
        if (!codom->type) {
            log_error(mod->log, &codom->loc, "invalid pi codomain", NULL);
            return NULL;
        }
        if (var && var->type != dom) {
            log_error(mod->log, &var->loc,
                "variable type '%0:e' does not match domain type '%1:e'",
                FMT_ARGS({ .e = var->type }, { .e = dom }));
            return NULL;
        }
    }
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

exp_t new_tup(mod_t mod, const exp_t* args, size_t arg_count, const struct loc* loc) {
    exp_t* prod_args = new_buf(exp_t, arg_count);
    for (size_t i = 0; i < arg_count; ++i)
        prod_args[i] = args[i]->type;
    exp_t type = new_prod(mod, prod_args, arg_count, loc);
    free_buf(prod_args);
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

static inline bool is_valid_index_type(exp_t index_type) {
    return index_type->tag == EXP_INT || index_type->tag == EXP_NAT;
}

static inline bool check_ext_or_ins(mod_t mod, exp_t val, exp_t index, exp_t* val_type, exp_t* elem_type, const char* msg) {
    if (!mod->log)
        return false;
    if (!val->type) {
        log_error(mod->log, &val->loc, "invalid %0:s value", FMT_ARGS({ .s = msg }));
        return false;
    }
    if (!index->type) {
        log_error(mod->log, &index->loc, "invalid %0:s index", FMT_ARGS({ .s = msg }));
        return false;
    }
    if (!is_valid_index_type(reduce_exp(index->type))) {
        log_error(mod->log, &index->loc, "%0:s index must be an integer", FMT_ARGS({ .s = msg }));
        return false;
    }
    (*val_type) = reduce_exp(val->type);
    switch ((*val_type)->tag) {
        case EXP_SUM:
        case EXP_PROD:
            if (index->tag != EXP_LIT || index->lit.int_val >= (*val_type)->prod.arg_count) {
                log_error(mod->log, &index->loc,
                    "%0:s index must be a literal smaller than %1:u",
                    FMT_ARGS({ .s = msg }, { .u = (*val_type)->prod.arg_count }));
                return false;
            }
            (*elem_type) = (*val_type)->prod.args[index->lit.int_val];
            break;
        default:
            log_error(mod->log, &val->loc, "invalid %0:s value type", FMT_ARGS({ .s = msg }));
            return false;
    }
    return true;
}

exp_t new_ins(mod_t mod, exp_t val, exp_t index, exp_t elem, const struct loc* loc) {
    exp_t val_type, elem_type;
    if (!check_ext_or_ins(mod, val, index, &val_type, &elem_type, "insertion"))
        return NULL;
    if (mod->log && (!elem->type || elem_type != reduce_exp(elem->type))) {
        log_error(mod->log, &elem->loc, "invalid insertion element", NULL);
        return NULL;
    }
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_INS,
        .type = val_type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .ins = {
            .val = val,
            .index = index,
            .elem = elem
        }
    });
}

exp_t new_ext(mod_t mod, exp_t val, exp_t index, const struct loc* loc) {
    exp_t val_type, elem_type;
    if (!check_ext_or_ins(mod, val, index, &val_type, &elem_type, "extract"))
        return NULL;
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_EXT,
        .type = elem_type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .ext = {
            .val = val,
            .index = index
        }
    });
}

static inline exp_t infer_abs_type(exp_t var, exp_t body) {
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
    if (mod->log && !left->type) {
        log_error(mod->log, &left->loc, "invalid application callee", NULL);
        return NULL;
    }
    exp_t callee_type = reduce_exp(left->type);
    if (mod->log && callee_type->tag != EXP_PI) {
        log_error(mod->log, &left->loc,
            "invalid type '%0:e' for application callee",
            FMT_ARGS({ .e = callee_type }));
        return NULL;
    }
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_ABS,
        .type = left->type->pi.var
            ? replace_exp(callee_type->pi.codom, callee_type->pi.var, right)
            : left->type->pi.codom,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .app = {
            .left = left,
            .right = right
        }
    });
}

static inline exp_t infer_let_type(const exp_t* vars, const exp_t* vals, size_t var_count, exp_t body_type) {
    // Replace bound variables in the expression and reduce it
    // until a fix point is reached. This may loop forever if
    // the expression does not terminate.
    bool todo;
    struct exp_map map = new_exp_map();
    for (size_t i = 0; i < var_count; ++i)
        insert_in_exp_map(&map, vars[i], vals[i]);
    do {
        exp_t old_type = body_type;
        body_type = replace_exps(body_type, &map);
        body_type = reduce_exp(body_type);
        todo = old_type != body_type;
    } while (todo);
    free_exp_map(&map);
    return body_type;
}

static inline exp_t new_let_or_letrec(mod_t mod, bool rec, const exp_t* vars, const exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    if (mod->log) {
        if (!body->type) {
            log_error(mod->log, &body->loc, "invalid let-expression body", NULL);
            return NULL;
        }
        for (size_t i = 0; i < var_count; ++i) {
            if (vars[i]->type != vals[i]->type) {
                log_error(mod->log, &vars[i]->loc,
                    "variable type '%0:e' does not match expression type '%1:e'",
                    FMT_ARGS({ .e = vars[i]->type }, { .e = vals[i]->type }));
                return NULL;
            }
        }
    }
    return insert_exp(mod, &(struct exp) {
        .tag = rec ? EXP_LETREC : EXP_LET,
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

exp_t new_let(mod_t mod, const exp_t* vars, const exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    return new_let_or_letrec(mod, false, vars, vals, var_count, body, loc);
}

exp_t new_letrec(mod_t mod, const exp_t* vars, const exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    return new_let_or_letrec(mod, true, vars, vals, var_count, body, loc);
}

exp_t new_match(mod_t mod, const exp_t* pats, const exp_t* vals, size_t pat_count, exp_t arg, const struct loc* loc) {
    if (mod->log) {
        if (pat_count == 0) {
            log_error(mod->log, loc, "match-expression requires at least one pattern", NULL);
            return NULL;
        }
        if (!vals[0]->type) {
            log_error(mod->log, &vals[0]->loc, "invalid match-expression value", NULL);
            return NULL;
        }
        for (size_t i = 0; i < pat_count; ++i) {
            if (!is_pat(pats[i])) {
                log_error(mod->log, &pats[i]->loc, "invalid pattern in match-expression", NULL);
                return NULL;
            }
        }
    }
    return insert_exp(mod, &(struct exp) {
        .tag = EXP_MATCH,
        .type = vals[0]->type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .match = {
            .pats = pats,
            .vals = vals,
            .pat_count = pat_count,
            .arg = arg
        }
    });
}

// Rebuild/Import/Replace ----------------------------------------------------------

exp_t rebuild_exp(exp_t exp) {
    return import_exp(get_mod(exp), exp);
}

exp_t import_exp(mod_t mod, exp_t exp) {
    switch (exp->tag) {
        case EXP_VAR:    return new_var(mod, exp->type, exp->var.index, &exp->loc);
        case EXP_UNI:    return new_uni(mod);
        case EXP_STAR:   return new_star(mod);
        case EXP_NAT:    return new_nat(mod);
        case EXP_WILD:   return new_wild(mod, exp->type, &exp->loc);
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
            return NULL;
    }
}

exp_t replace_exp(exp_t exp, exp_t from, exp_t to) {
    struct exp_map map = new_exp_map();
    insert_in_exp_map(&map, from, to);
    replace_exps(exp, &map);
    free_exp_map(&map);
    return exp;
}

static inline exp_t try_replace_exp(exp_t exp, struct exp_vec* stack, struct exp_map* map) {
    exp_t new_exp = deref_or_null((void**)find_in_exp_map(map, exp));
    if (new_exp)
        return new_exp;

    bool valid = true;
#define DEPENDS_ON(new, old) \
    new = deref_or_null((void**)find_in_exp_map(map, old)); \
    if (!new) { \
        valid = false; \
        push_to_exp_vec(stack, old); \
    }
    switch (exp->tag) {
        case EXP_UNI:
        case EXP_STAR:
        case EXP_NAT:
            new_exp = exp;
            break;
        case EXP_WILD:
        case EXP_TOP:
        case EXP_BOT:
        case EXP_LIT: {
            exp_t DEPENDS_ON(new_type, exp->type)
            if (valid) {
                new_exp = rebuild_exp(&(struct exp) {
                    .tag = exp->tag,
                    .type = new_type,
                    .lit = exp->lit,
                    .loc = exp->loc
                });
            }
            break;
        }
        case EXP_INT:
        case EXP_REAL: {
            exp_t DEPENDS_ON(new_bitwidth, exp->int_.bitwidth)
            if (valid) {
                new_exp = rebuild_exp(&(struct exp) {
                    .tag = exp->tag,
                    .type = exp->type,
                    .int_.bitwidth = new_bitwidth,
                    .loc = exp->loc
                });
            }
            break;
        }
        case EXP_VAR: {
            exp_t DEPENDS_ON(new_type, exp->type)
            if (valid)
                new_exp = new_var(get_mod(exp), new_type, exp->var.index, &exp->loc);
            break;
        }
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            exp_t* new_args = new_buf(exp_t, exp->tup.arg_count);
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                DEPENDS_ON(new_args[i], exp->tup.args[i]);
            }
            if (valid) {
                new_exp = import_exp(get_mod(exp), &(struct exp) {
                    .tag = exp->tag,
                    .tup = {
                        .arg_count = exp->tup.arg_count,
                        .args = new_args
                    },
                    .loc = exp->loc
                });
            }
            free_buf(new_args);
            break;
        }
        case EXP_INJ: {
            exp_t DEPENDS_ON(new_type, exp->type)
            exp_t DEPENDS_ON(new_arg, exp->inj.arg)
            if (valid)
                new_exp = new_inj(get_mod(exp), new_type, exp->inj.index, new_arg, &exp->loc);
            break;
        }
        case EXP_PI: {
            exp_t DEPENDS_ON(new_dom, exp->pi.dom)
            exp_t DEPENDS_ON(new_codom, exp->pi.codom)
            exp_t new_var = NULL;
            if (exp->pi.var) {
                DEPENDS_ON(new_var, exp->pi.var)
            }
            if (valid)
                new_exp = new_pi(get_mod(exp), new_var, new_dom, new_codom, &exp->loc);
            break;
        }
        case EXP_ABS: {
            exp_t DEPENDS_ON(new_var, exp->abs.var)
            exp_t DEPENDS_ON(new_body, exp->abs.body)
            if (valid)
                new_exp = new_abs(get_mod(exp), new_var, new_body, &exp->loc);
            break;
        }
        case EXP_APP: {
            exp_t DEPENDS_ON(new_left, exp->app.left)
            exp_t DEPENDS_ON(new_right, exp->app.right)
            if (valid)
                new_exp = new_app(get_mod(exp), new_left, new_right, &exp->loc);
            break;
        }
        case EXP_LET:
        case EXP_LETREC: {
            exp_t* new_vars = new_buf(exp_t, exp->let.var_count);
            exp_t* new_vals = new_buf(exp_t, exp->let.var_count);
            exp_t DEPENDS_ON(new_body, exp->let.body);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                DEPENDS_ON(new_vars[i], exp->let.vars[i])
                DEPENDS_ON(new_vals[i], exp->let.vals[i])
            }
            if (valid) {
                new_exp = import_exp(get_mod(exp), &(struct exp) {
                    .tag = exp->tag,
                    .let = {
                        .vars = new_vars,
                        .vals = new_vals,
                        .var_count = exp->let.var_count,
                        .body = new_body
                    },
                    .loc = exp->loc
                });
            }
            free_buf(new_vars);
            free_buf(new_vals);
            break;
        }
        case EXP_MATCH: {
            exp_t* new_pats = new_buf(exp_t, exp->match.pat_count);
            exp_t* new_vals = new_buf(exp_t, exp->match.pat_count);
            exp_t DEPENDS_ON(new_arg, exp->match.arg);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                DEPENDS_ON(new_pats[i], exp->match.pats[i])
                DEPENDS_ON(new_vals[i], exp->match.vals[i])
            }
            if (valid) {
                new_exp = new_match(get_mod(exp),
                    new_pats, new_vals,
                    exp->match.pat_count,
                    new_arg, &exp->loc);
            }
            free_buf(new_pats);
            free_buf(new_vals);
            break;
        }
        default:
            assert(false);
            break;
    }
#undef DEPENDS_ON
    if (new_exp)
        insert_in_exp_map(map, exp, new_exp);
    return new_exp;
}

exp_t replace_exps(exp_t exp, struct exp_map* map) {
    struct exp_vec stack = new_exp_vec();
    push_to_exp_vec(&stack, exp);
    while (stack.size > 0) {
        exp_t exp = stack.elems[stack.size - 1];
        if (try_replace_exp(exp, &stack, map))
            pop_from_exp_vec(&stack);
    }
    free_exp_vec(&stack);
    return *find_in_exp_map(map, exp);
}

exp_t reduce_exp(exp_t exp) {
    bool todo;
    do {
        exp_t old_exp = exp;
        while (exp->tag == EXP_APP && exp->app.left->tag == EXP_ABS)
            exp = replace_exp(exp->app.left->abs.body, exp->app.left->abs.var, exp->app.right);
        while (exp->tag == EXP_LET || exp->tag == EXP_LETREC) {
            struct exp_map map = new_exp_map();
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i)
                insert_in_exp_map(&map, exp->let.vars[i], exp->let.vals[i]);
            exp_t new_body = replace_exps(exp->let.body, &map);
            if (exp->tag == EXP_LETREC) {
                exp_t* new_vals = new_buf(exp_t, exp->letrec.var_count);
                for (size_t i = 0, n = exp->letrec.var_count; i < n; ++i)
                    new_vals[i] = replace_exps(exp->letrec.vals[i], &map);
                exp = new_letrec(get_mod(exp),
                    exp->letrec.vars, new_vals,
                    exp->letrec.var_count,
                    new_body, &exp->loc);
                free_buf(new_vals);
            } else
                exp = new_body;
            free_exp_map(&map);
        }
        todo = old_exp != exp;
    } while (todo);
    return exp;
}
