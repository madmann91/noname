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
};

static bool cmp_exp(const void* ptr1, const void* ptr2) {
    exp_t exp1 = *(exp_t*)ptr1, exp2 = *(exp_t*)ptr2;
    if (exp1->tag != exp2->tag || exp1->type != exp2->type)
        return false;
    switch (exp1->tag) {
        case EXP_BVAR:
            return
                exp1->bvar.index == exp2->bvar.index &&
                exp1->bvar.sub_index == exp2->bvar.index;
        case EXP_FVAR:
            return exp1->fvar.index == exp2->fvar.index;
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
            return
                exp1->let.body == exp2->let.body &&
                exp1->let.bind_count == exp2->let.bind_count &&
                !memcmp(exp1->let.binds, exp2->let.binds, sizeof(exp_t) * exp1->let.bind_count);
        case EXP_MATCH:
            return
                exp1->match.arg == exp2->match.arg &&
                exp1->match.pat_count == exp2->match.pat_count &&
                !memcmp(exp1->match.exps, exp2->match.exps, sizeof(exp_t) * exp1->match.pat_count) &&
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
        case EXP_BVAR:
            hash = hash_uint(hash, exp->bvar.index);
            hash = hash_uint(hash, exp->bvar.sub_index);
            break;
        case EXP_FVAR:
            hash = hash_uint(hash, exp->fvar.index);
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
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i)
                hash = hash_ptr(hash, exp->let.binds[i]);
            hash = hash_ptr(hash, exp->let.body);
            break;
        case EXP_MATCH:
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                hash = hash_ptr(hash, exp->match.pats[i]);
                hash = hash_ptr(hash, exp->match.exps[i]);
            }
            hash = hash_ptr(hash, exp->match.arg);
            break;
    }
    return hash;
}

mod_t new_mod(void) {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->exps = new_htable(sizeof(exp_t), DEFAULT_CAP, cmp_exp);
    mod->arena = new_arena(DEFAULT_ARENA_SIZE);
    return mod;
}

void free_mod(mod_t mod) {
    free_htable(&mod->exps);
    free_arena(mod->arena);
    free(mod);
}

mod_t get_mod_from_exp(exp_t exp) {
    while (exp->tag != EXP_UNI)
        exp = exp->type;
    return exp->uni.mod;
}

exp_t rebuild_exp(exp_t exp) {
    return import_exp(get_mod_from_exp(exp), exp);
}

static inline exp_t* copy_exps(mod_t mod, const exp_t* exps, size_t count) {
    exp_t* new_exps = alloc_in_arena(&mod->arena, sizeof(exp_t) * count);
    memcpy(new_exps, exps, sizeof(exp_t) * count);
    return new_exps;
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
            new_exp->match.pats = copy_exps(mod, exp->match.pats, exp->match.pat_count);
            break;
        default:
            break;
    }

    exp_t copy = new_exp;
    bool ok = insert_in_htable(&mod->exps, &copy, hash, NULL);
    assert(ok); (void)ok;
    return new_exp;
}

struct action {
    enum {
        ACTION_OPEN  = 0x01,
        ACTION_CLOSE = 0x02,
        ACTION_SHIFT = 0x04
    } tag;
    union {
        struct {
            exp_t* fvs;
            size_t fv_count;
        } open_or_close;
        struct {
            size_t inc;
            bool dir;
        } shift;
    };
};

static exp_t traverse_exp(size_t index, exp_t exp, struct action* action) {
    if (exp->tag == EXP_UNI || exp->tag == EXP_STAR || exp->tag == EXP_NAT)
        return exp;

    exp_t new_type = traverse_exp(index, exp->type, action);
    switch (exp->tag) {
        case EXP_BVAR:
            if (action->tag == ACTION_OPEN && exp->bvar.index == index) {
                assert(exp->bvar.sub_index < action->open_or_close.fv_count);
                return action->open_or_close.fvs[exp->bvar.sub_index];
            } else if (action->tag == ACTION_SHIFT && exp->bvar.index >= index) {
                return rebuild_exp(&(struct exp) {
                    .tag  = EXP_BVAR,
                    .type = new_type,
                    .bvar = {
                        .index = action->shift.dir
                            ? exp->bvar.index + action->shift.inc
                            : exp->bvar.index - action->shift.inc,
                        .sub_index = exp->bvar.sub_index
                    }
                });
            }
            break;
        case EXP_FVAR:
            if (!(action->tag & ACTION_CLOSE))
                break;
            for (size_t i = 0, n = action->open_or_close.fv_count; i < n; ++i) {
                if (exp == action->open_or_close.fvs[i]) {
                    return rebuild_exp(&(struct exp) {
                        .tag  = EXP_BVAR,
                        .type = new_type,
                        .bvar = {
                            .index = index,
                            .sub_index = i
                        }
                    });
                }
            }
            break;
        default:
            assert(false && "invalid expression tag");
            // fallthrough
        case EXP_WILD:
        case EXP_TOP:
        case EXP_BOT:
        case EXP_LIT:
            break;
        case EXP_INT:
        case EXP_REAL:
            return rebuild_exp(&(struct exp) {
                .tag           = exp->tag,
                .type          = new_type,
                .real.bitwidth = traverse_exp(index, exp->real.bitwidth, action),
            });
        case EXP_PI:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_PI,
                .type = new_type,
                .pi   = {
                    .dom   = traverse_exp(index, exp->pi.dom, action),
                    .codom = traverse_exp(index + 1, exp->pi.codom, action)
                }
            });
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            NEW_BUF(new_args, exp_t, exp->tup.arg_count)
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                new_args[i] = traverse_exp(index, exp->tup.args[i], action);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = new_type,
                .tup  = {
                    .args      = new_args,
                    .arg_count = exp->tup.arg_count
                }
            });
            FREE_BUF(new_args);
            return new_exp;
        }
        case EXP_INJ:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_INJ,
                .type = new_type,
                .inj  = {
                    .arg   = traverse_exp(index, exp->inj.arg, action),
                    .index = exp->inj.index
                }
            });
        case EXP_ABS:
            return rebuild_exp(&(struct exp) {
                .tag      = EXP_ABS,
                .type     = new_type,
                .abs.body = traverse_exp(index + 1, exp->abs.body, action)
            });
        case EXP_APP:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_APP,
                .type = new_type,
                .app  = {
                    .left  = traverse_exp(index + 1, exp->app.left, action),
                    .right = traverse_exp(index + 1, exp->app.right, action)
                }
            });
        case EXP_LET: {
            NEW_BUF(new_binds, exp_t, exp->let.bind_count)
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i)
                new_binds[i] = traverse_exp(index + 1, exp->let.binds[i], action);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag  = EXP_LET,
                .type = new_type,
                .let  = {
                    .binds      = new_binds,
                    .bind_count = exp->let.bind_count,
                    .body       = traverse_exp(index + 1, exp->let.body, action)
                }
            });
            FREE_BUF(new_binds);
            return new_exp;
        }
        case EXP_MATCH: {
            NEW_BUF(new_exps, exp_t, exp->match.pat_count)
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i)
                new_exps[i] = traverse_exp(index + 1, exp->match.exps[i], action);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag   = EXP_MATCH,
                .type  = new_type,
                .match = {
                    .arg       = traverse_exp(index + 1, exp->match.arg, action),
                    .pats      = exp->match.pats,
                    .exps      = new_exps,
                    .pat_count = exp->match.pat_count
                }
            });
            FREE_BUF(new_exps);
            return new_exp;
        }
    }

    if (new_type == exp->type)
        return exp;
    struct exp copy = *exp;
    copy.type = new_type;
    return rebuild_exp(&copy);
}

exp_t open_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    return traverse_exp(index, exp, &(struct action) {
        .tag           = ACTION_OPEN,
        .open_or_close = {
            .fvs       = fvs,
            .fv_count  = fv_count
        }
    });
}

exp_t close_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    return traverse_exp(index, exp, &(struct action) {
        .tag           = ACTION_CLOSE,
        .open_or_close = {
            .fvs       = fvs,
            .fv_count  = fv_count
        }
    });
}

exp_t shift_exp(size_t index, exp_t exp, size_t shift_inc, bool shift_dir) {
    return traverse_exp(index, exp, &(struct action) {
        .tag   = ACTION_SHIFT,
        .shift = {
            .dir = shift_dir,
            .inc = shift_inc
        }
    });
}
