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
    struct htable fvs;
    arena_t arena;
    exp_t uni, star, nat;
};

struct exp_pair { exp_t fst, snd; };

// Free variables ------------------------------------------------------------------

static inline bool cmp_fvs(const void* ptr1, const void* ptr2) {
    fvs_t fvs1 = *(fvs_t*)ptr1, fvs2 = *(fvs_t*)ptr2;
    return
        fvs1->count == fvs2->count &&
        !memcmp(fvs1->vars, fvs2->vars, sizeof(exp_t) * fvs1->count);
}

static inline uint32_t hash_fvs(fvs_t fvs) {
    uint32_t h = FNV_OFFSET;
    for (size_t i = 0, n = fvs->count; i < n; ++i)
        h = hash_ptr(h, fvs->vars[i]);
    return h;
}

static inline fvs_t insert_fvs(mod_t mod, fvs_t fvs) {
    uint32_t hash = hash_fvs(fvs);
    fvs_t* found = find_in_htable(&mod->fvs, &fvs, hash);
    if (found)
        return *found;

    struct fvs* new_fvs = alloc_from_arena(&mod->arena, sizeof(struct fvs));
    new_fvs->vars = alloc_from_arena(&mod->arena, sizeof(exp_t) * fvs->count);
    new_fvs->count = fvs->count;
    memcpy(new_fvs->vars, fvs->vars, sizeof(exp_t) * fvs->count);
    fvs_t copy = new_fvs;
    insert_in_htable(&mod->fvs, &copy, hash_fvs(fvs), NULL);
    return new_fvs;
}

static inline bool cmp_vars(const void* a, const void* b) { return (*(exp_t*)a) < (*(exp_t*)b); }
SHELL_SORT(sort_vars, exp_t, cmp_vars)

fvs_t new_fvs(mod_t mod, const exp_t* vars, size_t count) {
    NEW_BUF(sorted_vars, exp_t, count)
    memcpy(sorted_vars, vars, sizeof(exp_t) * count);
    sort_vars(sorted_vars, count);
#ifndef NDEBUG
    for (size_t i = 1; i < count; ++i)
        assert(sorted_vars[i - 1] < sorted_vars[i]);
#endif
    fvs_t fvs = insert_fvs(mod, &(struct fvs) { .vars = sorted_vars, .count = count });
    FREE_BUF(sorted_vars);
    return fvs;
}

fvs_t new_fv(mod_t mod, exp_t var) {
    assert(var->tag == EXP_VAR);
    return insert_fvs(mod, &(struct fvs) {
        .vars = (exp_t*)&var,
        .count = 1
    });
}

fvs_t union_fvs(mod_t mod, fvs_t fvs1, fvs_t fvs2) {
    NEW_BUF(vars, exp_t, fvs1->count + fvs2->count)
    size_t i = 0, j = 0, count = 0;
    while (i < fvs1->count && j < fvs2->count) {
        if (fvs1->vars[i] < fvs2->vars[j])
            vars[count++] = fvs1->vars[i++];
        else if (fvs1->vars[i] > fvs2->vars[j])
            vars[count++] = fvs2->vars[j++];
        else
            vars[count++] = fvs1->vars[i++], j++;
    }
    while (i < fvs1->count) vars[count++] = fvs1->vars[i++];
    while (j < fvs2->count) vars[count++] = fvs2->vars[j++];
    fvs_t res = new_fvs(mod, vars, count);
    FREE_BUF(vars);
    return res;
}

fvs_t intr_fvs(mod_t mod, fvs_t fvs1, fvs_t fvs2) {
    size_t min_count = fvs1->count < fvs2->count ? fvs1->count : fvs2->count;
    NEW_BUF(vars, exp_t, min_count)
    size_t i = 0, j = 0, count = 0;
    while (i < fvs1->count && j < fvs2->count) {
        if (fvs1->vars[i] < fvs2->vars[j])
            i++;
        else if (fvs1->vars[i] > fvs2->vars[j])
            j++;
        else
            vars[count++] = fvs1->vars[i++], j++;
    }
    fvs_t res = new_fvs(mod, vars, count);
    FREE_BUF(vars);
    return res;
}

fvs_t diff_fvs(mod_t mod, fvs_t fvs1, fvs_t fvs2) {
    NEW_BUF(vars, exp_t, fvs1->count)
    size_t i = 0, j = 0, count = 0;
    while (i < fvs1->count && j < fvs2->count) {
        if (fvs1->vars[i] < fvs2->vars[j])
            vars[count++] = fvs1->vars[i++];
        else if (fvs1->vars[i] > fvs2->vars[j])
            j++;
        else
            i++, j++;
    }
    while (i < fvs1->count) vars[count++] = fvs1->vars[i++];
    fvs_t res = new_fvs(mod, vars, count);
    FREE_BUF(vars);
    return res;
}

bool contains_fvs(fvs_t fvs1, fvs_t fvs2) {
    size_t i = 0, j = 0;
    while (i < fvs1->count && j < fvs2->count) {
        if (fvs1->vars[i] < fvs2->vars[j])
            i++;
        else if (fvs1->vars[i] > fvs2->vars[j])
            j++;
        else
            return true;
    }
    return false;
}

bool contains_fv(fvs_t fvs, exp_t var) {
    assert(var->tag == EXP_VAR);
    if (fvs->count == 0)
        return false;
    size_t i = 0, j = fvs->count - 1;
    while (i <= j) {
        size_t m = (i + j) / 2;
        if (fvs->vars[m] < var)
            i = m + 1;
        else if (fvs->vars[m] > var) {
            if (m == 0) return false;
            j = m - 1;
        } else
            return true;
    }
    return false;
}

// Expressions ---------------------------------------------------------------------

static bool cmp_exp_pair(const void* ptr1, const void* ptr2) {
    exp_t exp1 = ((struct exp_pair*)ptr1)->fst;
    exp_t exp2 = ((struct exp_pair*)ptr2)->fst;
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

exp_t simplify_exp(mod_t, exp_t);

static inline exp_t insert_exp(mod_t mod, exp_t exp) {
    uint32_t hash = hash_exp(exp);
    struct exp_pair* found = find_in_htable(&mod->exps, &(struct exp_pair) { .fst = exp }, hash);
    if (found)
        return found->snd;

    struct exp* new_exp = alloc_from_arena(&mod->arena, sizeof(struct exp));
    memcpy(new_exp, exp, sizeof(struct exp));
    new_exp->fvs = exp->type ? exp->type->fvs : new_fvs(mod, NULL, 0);
    new_exp->depth = 0;

    // Copy the data contained in the original expression and compute properties
    switch (exp->tag) {
        case EXP_INT:
        case EXP_REAL:
            new_exp->depth = max_depth(new_exp, exp->real.bitwidth);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->real.bitwidth->fvs);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                new_exp->depth = max_depth(new_exp, exp->tup.args[i]);
                new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->tup.args[i]->fvs);
            }
            new_exp->tup.args = copy_exps(mod, exp->tup.args, exp->tup.arg_count);
            break;
        case EXP_INJ:
            new_exp->depth = max_depth(new_exp, exp->inj.arg);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->inj.arg->fvs);
            break;
        case EXP_PI:
            new_exp->depth = max_depth(new_exp, exp->pi.dom);
            new_exp->depth = max_depth(new_exp, exp->pi.codom);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->pi.dom->fvs);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->pi.codom->fvs);
            if (exp->pi.var)
                new_exp->fvs = diff_fvs(mod, new_exp->fvs, exp->pi.var->fvs);
            new_exp->depth++;
            break;
        case EXP_ABS:
            new_exp->depth = max_depth(new_exp, exp->abs.body) + 1;
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->abs.body->fvs);
            new_exp->fvs = diff_fvs(mod, new_exp->fvs, exp->abs.var->fvs);
            break;
        case EXP_APP:
            new_exp->depth = max_depth(new_exp, exp->app.left);
            new_exp->depth = max_depth(new_exp, exp->app.right);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->app.left->fvs);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->app.right->fvs);
            break;
        case EXP_LET:
        case EXP_LETREC:
            new_exp->depth = max_depth(new_exp, exp->let.body);
            new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->let.body->fvs);
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i) {
                new_exp->depth = max_depth(new_exp, exp->let.vals[i]);
                new_exp->fvs = union_fvs(mod, new_exp->fvs, exp->let.vals[i]->fvs);
            }
            for (size_t i = 0, n = exp->let.var_count; i < n; ++i)
                new_exp->fvs = diff_fvs(mod, new_exp->fvs, exp->let.vars[i]->fvs);
            new_exp->let.vars = copy_exps(mod, exp->let.vars, exp->let.var_count);
            new_exp->let.vals = copy_exps(mod, exp->let.vals, exp->let.var_count);
            new_exp->depth += exp->let.var_count;
            break;
        case EXP_MATCH:
            new_exp->match.vals = copy_exps(mod, exp->match.vals, exp->match.pat_count);
            new_exp->match.pats = copy_exps(mod, exp->match.pats, exp->match.pat_count);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                new_exp->depth = max_depth(new_exp, exp->match.vals[i]);
                new_exp->fvs = union_fvs(mod, new_exp->fvs,
                    diff_fvs(mod, exp->match.vals[i]->fvs, exp->match.pats[i]->fvs));
            }
            new_exp->depth += exp->match.pat_count;
            break;
        case EXP_VAR:
            new_exp->fvs = new_fv(mod, new_exp);
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
    bool ok = insert_in_htable(&mod->exps, &(struct exp_pair) { .fst = new_exp, .snd = res }, hash, NULL);
    assert(ok); (void)ok;
    return res;
}

// Module --------------------------------------------------------------------------

mod_t new_mod(void) {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->exps = new_htable(sizeof(struct exp_pair), DEFAULT_CAP, cmp_exp_pair);
    mod->fvs  = new_htable(sizeof(fvs_t), DEFAULT_CAP, cmp_fvs);
    mod->arena = new_arena(DEFAULT_ARENA_SIZE);
    mod->uni  = insert_exp(mod, &(struct exp) { .tag = EXP_UNI,  .uni.mod = mod });
    mod->star = insert_exp(mod, &(struct exp) { .tag = EXP_STAR, .type = mod->uni });
    mod->nat  = insert_exp(mod, &(struct exp) { .tag = EXP_NAT,  .type = mod->star });
    return mod;
}

void free_mod(mod_t mod) {
    free_htable(&mod->exps);
    free_htable(&mod->fvs);
    free_arena(mod->arena);
    free(mod);
}

// Helpers -------------------------------------------------------------------------

mod_t get_mod(exp_t exp) {
    while (exp->tag != EXP_UNI)
        exp = exp->type;
    return exp->uni.mod;
}

bool is_pat(exp_t exp) {
    // TODO: Check that the expression is a valid pattern
    (void)exp;
    return true;
}

bool is_trivial_pat(exp_t exp) {
    // TODO: Check that the expression is a trivial (always matching) pattern
    (void)exp;
    return false;
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
    assert(var->type == dom);
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
    exp_t callee_type = reduce_exp(left->type);
    assert(callee_type->tag == EXP_PI);
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

static inline exp_t infer_let_type(exp_t* vars, exp_t* vals, size_t var_count, exp_t body_type) {
    // Replace bound variables in the expression and reduce it
    // until a fix point is reached. This may loop forever if
    // the expression does not terminate.
    bool todo;
    struct htable map = new_exp_map();
    for (size_t i = 0; i < var_count; ++i)
        insert_in_exp_map(&map, vars[i], vals[i]);
    do {
        exp_t old_type = body_type;
        body_type = replace_exps(body_type, &map);
        body_type = reduce_exp(body_type);
        todo = old_type != body_type;
    } while (todo);
    free_htable(&map);
    return body_type;
}

static inline exp_t new_let_or_letrec(mod_t mod, bool rec, exp_t* vars, exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
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

exp_t new_let(mod_t mod, exp_t* vars, exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    return new_let_or_letrec(mod, false, vars, vals, var_count, body, loc);
}

exp_t new_letrec(mod_t mod, exp_t* vars, exp_t* vals, size_t var_count, exp_t body, const struct loc* loc) {
    return new_let_or_letrec(mod, true, vars, vals, var_count, body, loc);
}

exp_t new_match(mod_t mod, exp_t* pats, exp_t* vals, size_t pat_count, exp_t arg, const struct loc* loc) {
    assert(pat_count > 0);
#ifndef NDEBUG
    for (size_t i = 0; i < pat_count; ++i)
        assert(is_pat(pats[i]));
#endif
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

// Expression map/set --------------------------------------------------------------

static inline bool cmp_exp_pair_by_addr(const void* ptr1, const void* ptr2) {
    return ((const struct exp_pair*)ptr1)->fst == ((const struct exp_pair*)ptr2)->fst;
}

static inline bool cmp_exp_by_addr(const void* ptr1, const void* ptr2) {
    return *(exp_t*)ptr1 == *(exp_t*)ptr2;
}

struct htable new_exp_map(void) {
    return new_htable(sizeof(struct exp_pair), DEFAULT_CAP, cmp_exp_pair_by_addr);
}

struct htable new_exp_set(void) {
    return new_htable(sizeof(exp_t), DEFAULT_CAP, cmp_exp_by_addr);
}

exp_t find_in_exp_map(struct htable* map, exp_t exp) {
    struct exp_pair* found = find_in_htable(map, &(struct exp_pair) { .fst = exp }, hash_ptr(FNV_OFFSET, exp));
    return found ? found->snd : NULL;
}

bool insert_in_exp_map(struct htable* map, exp_t from, exp_t to) {
    return insert_in_htable(map, &(struct exp_pair) { .fst = from, .snd = to }, hash_ptr(FNV_OFFSET, from), NULL);
}

bool find_in_exp_set(struct htable* set, exp_t exp) {
    return find_in_htable(set, &exp, hash_ptr(FNV_OFFSET, exp)) != NULL;
}

bool insert_in_exp_set(struct htable* set, exp_t exp) {
    return insert_in_htable(set, &exp, hash_ptr(FNV_OFFSET, exp), NULL);
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
    struct htable map = new_exp_map();
    insert_in_exp_map(&map, from, to);
    replace_exps(exp, &map);
    free_htable(&map);
    return exp;
}

exp_t replace_exps(exp_t exp, struct htable* map) {
    (void)map;
    return exp;
}

exp_t reduce_exp(exp_t exp) {
    // TODO: Implement reduction
    return exp;
}
