#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "exp.h"
#include "utils.h"
#include "htable.h"
#include "arena.h"

struct mod {
    struct htable exps, pats;
    arena_t arena;
};

static bool cmp_exp(const void* ptr1, const void* ptr2) {
    exp_t exp1 = *(exp_t*)ptr1;
    exp_t exp2 = *(exp_t*)ptr2;
    unsigned tag = exp1->tag;
    if (tag != exp2->tag || exp1->type != exp2->type)
        return false;
    switch (tag) {
        case EXP_BVAR:
            return
                exp1->bvar.index == exp2->bvar.index &&
                exp1->bvar.sub_index == exp2->bvar.index;
        case EXP_FVAR:
            return exp1->fvar.name == exp2->fvar.name;
        case EXP_UNI:
            return exp1->uni.mod == exp2->uni.mod;
        case EXP_TOP:
        case EXP_BOT:
        case EXP_STAR:
            return true;
        case EXP_INT:
        case EXP_REAL:
            return exp1->real.bitwidth == exp2->real.bitwidth;
        case EXP_LIT:
            return !memcmp(&exp1->lit, &exp2->lit, sizeof(union lit));
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
            return
                exp1->let.body == exp2->let.body &&
                exp1->let.bind_count == exp2->let.bind_count &&
                !memcmp(exp1->let.binds, exp2->let.binds, sizeof(exp_t) * exp1->let.bind_count);
        case EXP_MATCH:
            return
                exp1->match.arg == exp2->match.arg &&
                exp1->match.pat_count == exp2->match.pat_count &&
                !memcmp(exp1->match.exps, exp2->match.exps, sizeof(exp_t) * exp1->match.pat_count) &&
                !memcmp(exp1->match.pats, exp2->match.pats, sizeof(pat_t) * exp1->match.pat_count);
        default:
            assert(false);
            return false;
    }
}

static inline uint32_t hash_exp(exp_t exp) {
    // TODO
    return 0;
}

static bool cmp_pat(const void* ptr1, const void* ptr2) {
    // TODO
    return false;
}

mod_t new_mod(void) {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->exps = new_htable(sizeof(struct exp), DEFAULT_CAP, cmp_exp);
    mod->pats = new_htable(sizeof(struct pat), DEFAULT_CAP, cmp_pat);
    mod->arena = new_arena(DEFAULT_ARENA_SIZE);
    return mod;
}

void free_mod(mod_t mod) {
    free_htable(&mod->exps);
    free_htable(&mod->pats);
    free_arena(mod->arena);
    free(mod);
}

mod_t get_mod_from_exp(exp_t exp) {
    while (exp->tag != EXP_UNI)
        exp = exp->type;
    return exp->uni.mod;
}

mod_t get_mod_from_pat(pat_t pat) {
    return get_mod_from_exp(pat->type);
}

exp_t rebuild_exp(exp_t exp) {
    return import_exp(get_mod_from_exp(exp), exp);
}

static inline exp_t* copy_exps(mod_t mod, const exp_t* exps, size_t count) {
    exp_t* new_exps = alloc_in_arena(&mod->arena, sizeof(exp_t*) * count);
    memcpy(new_exps, exps, sizeof(exp_t*) * count);
    return new_exps;
}

static inline pat_t* copy_pats(mod_t mod, const pat_t* pats, size_t count) {
    pat_t* new_pats = alloc_in_arena(&mod->arena, sizeof(pat_t*) * count);
    memcpy(new_pats, pats, sizeof(exp_t*) * count);
    return new_pats;
}

exp_t import_exp(mod_t mod, exp_t exp) {
    uint32_t hash = hash_exp(exp);
    exp_t* found = find_in_htable(&mod->exps, &exp, hash);
    if (found)
        return *found;

    struct exp* new_exp = alloc_in_arena(&mod->arena, sizeof(struct exp));
    memcpy(new_exp, exp, sizeof(struct exp));

    // Copy the data contained in the original expression
    switch (exp->tag) {
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            new_exp->tup.args = copy_exps(mod, exp->tup.args, exp->tup.arg_count);
            break;
        case EXP_LET:
            new_exp->let.binds = copy_exps(mod, exp->let.binds, exp->let.bind_count);
            break;
        case EXP_MATCH:
            new_exp->match.exps = copy_exps(mod, exp->match.exps, exp->match.pat_count);
            new_exp->match.pats = copy_pats(mod, exp->match.pats, exp->match.pat_count);
            break;
        default:
            break;
    }

    // Save the current expression before insertion
    exp = new_exp;
    bool success = insert_in_htable(&mod->exps, &new_exp, hash, NULL);
    assert(success); (void)success;
    return exp;
}

static exp_t open_or_close_exp(bool open, size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    switch (exp->tag) {
        case EXP_BVAR:
            if (open && exp->bvar.index == index) {
                assert(exp->bvar.sub_index < fv_count);
                return fvs[exp->bvar.sub_index];
            }
            break;
        case EXP_FVAR:
            if (!open) {
                for (size_t i = 0; i < fv_count; ++i) {
                    if (exp == fvs[i]) {
                        return rebuild_exp(&(struct exp) {
                            .tag  = EXP_BVAR,
                            .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                            .bvar = {
                                .index = index,
                                .sub_index = i
                            }
                        });
                    }
                }
            }
            break;
        case EXP_LET: {
            NEW_BUF(new_binds, exp_t, exp->let.bind_count)
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i)
                new_binds[i] = open_or_close_exp(open, index + 1, exp->let.binds[i], fvs, fv_count);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag  = EXP_LET,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .let  = {
                    .binds      = new_binds,
                    .bind_count = exp->let.bind_count,
                    .body       = open_or_close_exp(open, index + 1, exp->let.body, fvs, fv_count)
                }
            });
            FREE_BUF(new_binds)
            return new_exp;
        }
        case EXP_PI:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_PI,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .pi   = {
                    .dom   = open_or_close_exp(open, index, exp->pi.dom, fvs, fv_count),
                    .codom = open_or_close_exp(open, index + 1, exp->pi.codom, fvs, fv_count)
                }
            });
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            NEW_BUF(new_args, exp_t, exp->tup.arg_count)
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                new_args[i] = open_or_close_exp(open, index, exp->tup.args[i], fvs, fv_count);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .tup  = {
                    .args      = new_args,
                    .arg_count = exp->tup.arg_count
                }
            });
            FREE_BUF(new_args)
            return new_exp;
        }
        case EXP_INJ:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_INJ,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .inj  = {
                    .arg   = open_or_close_exp(open, index, exp->inj.arg, fvs, fv_count),
                    .index = exp->inj.index
                }
            });
        case EXP_ABS:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_ABS,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .abs.body = open_or_close_exp(open, index + 1, exp->abs.body, fvs, fv_count)
            });
        case EXP_APP:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_APP,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .app  = {
                    .left  = open_or_close_exp(open, index + 1, exp->app.left, fvs, fv_count),
                    .right = open_or_close_exp(open, index + 1, exp->app.right, fvs, fv_count)
                }
            });
        case EXP_UNI:
        case EXP_STAR:
            return exp;
        default:
            assert(false && "invalid expression tag");
            // fallthrough
        case EXP_TOP:
        case EXP_BOT:
        case EXP_LIT:
            break;
    }

    exp_t new_type = open_or_close_exp(open, index, exp->type, fvs, fv_count);
    if (new_type == exp->type)
        return exp;
    struct exp copy = *exp;
    copy.type = new_type;
    return rebuild_exp(&copy);
}

exp_t open_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    return open_or_close_exp(true, index, exp, fvs, fv_count);
}

exp_t close_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    return open_or_close_exp(false, index, exp, fvs, fv_count);
}
