#include <assert.h>
#include <stdbool.h>
#include "exp.h"
#include "utils.h"

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

exp_t import_exp(mod_t mod, exp_t exp) {
    // TODO
}

static inline exp_t open_exp_type(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    exp_t opened_type = open_exp(index, exp->type, fvs, fv_count);
    if (opened_type == exp->type)
        return exp;
    struct exp copy = *exp;
    copy.type = opened_type;
    return rebuild_exp(&copy);
}

exp_t open_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    switch (exp->tag) {
        case EXP_BVAR:
            if (exp->bvar.index == index) {
                assert(exp->bvar.sub_index < fv_count);
                return fvs[exp->bvar.sub_index];
            }
            return open_exp_type(index, exp, fvs, fv_count);
        case EXP_LET: {
            ALLOC_BUF(opened_binds, exp_t, exp->let.bind_count)
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i) {
                opened_binds[i] = open_exp(
                    index + 1,
                    exp->let.binds[i],
                    fvs, fv_count);
            }
            exp_t opened_exp = rebuild_exp(&(struct exp) {
                .tag  = EXP_LET,
                .type = open_exp(index, exp->type, fvs, fv_count),
                .let  = {
                    .binds      = opened_binds,
                    .bind_count = exp->let.bind_count,
                    .body       = open_exp(index + 1, exp->let.body, fvs, fv_count)
                }
            });
            FREE_BUF(opened_binds)
            return opened_exp;
        }
        case EXP_PI:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_PI,
                .type = open_exp(index, exp->type, fvs, fv_count),
                .pi   = {
                    .dom   = open_exp(index, exp->pi.dom, fvs, fv_count),
                    .codom = open_exp(index + 1, exp->pi.codom, fvs, fv_count)
                }
            });
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            ALLOC_BUF(opened_args, exp_t, exp->tup.arg_count)
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                opened_args[i] = open_exp(index, exp->tup.args[i], fvs, fv_count);
            exp_t opened_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = open_exp(index, exp->type, fvs, fv_count),
                .tup  = {
                    .args      = opened_args,
                    .arg_count = exp->tup.arg_count
                }
            });
            FREE_BUF(opened_args)
            return opened_exp;
        }
        case EXP_INJ:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_INJ,
                .type = open_exp(index, exp->type, fvs, fv_count),
                .inj  = {
                    .arg   = open_exp(index, exp->inj.arg, fvs, fv_count),
                    .index = exp->inj.index
                }
            });
        case EXP_ABS:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_ABS,
                .type = open_exp(index, exp->type, fvs, fv_count),
                .abs  = {
                    .pat  = exp->abs.pat,
                    .body = open_exp(index + 1, exp->abs.body, fvs, fv_count)
                }
            });
        case EXP_APP:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_APP,
                .type = open_exp(index, exp->type, fvs, fv_count),
                .app  = {
                    .left  = open_exp(index + 1, exp->app.left, fvs, fv_count),
                    .right = open_exp(index + 1, exp->app.right, fvs, fv_count)
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
        case EXP_FVAR:
        case EXP_LIT:
            return open_exp_type(index, exp, fvs, fv_count);
    }
}

static inline exp_t close_exp_type(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    exp_t closed_type = close_exp(index, exp->type, fvs, fv_count);
    if (closed_type == exp->type)
        return exp;
    struct exp copy = *exp;
    copy.type = closed_type;
    return rebuild_exp(&copy);
}

exp_t close_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    switch (exp->tag) {
        case EXP_FVAR:
            for (size_t i = 0; i < fv_count; ++i) {
                if (exp == fvs[i]) {
                    return rebuild_exp(&(struct exp) {
                        .tag  = EXP_BVAR,
                        .type = close_exp(index, exp->type, fvs, fv_count),
                        .bvar = {
                            .index = index,
                            .sub_index = i
                        }
                    });
                }
            }
            return close_exp_type(index, exp, fvs, fv_count);
        case EXP_LET: {
            ALLOC_BUF(closed_binds, exp_t, exp->let.bind_count)
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i) {
                closed_binds[i] = close_exp(
                    index + 1,
                    exp->let.binds[i],
                    fvs, fv_count);
            }
            exp_t closed_exp = rebuild_exp(&(struct exp) {
                .tag  = EXP_LET,
                .type = close_exp(index, exp->type, fvs, fv_count),
                .let  = {
                    .binds      = closed_binds,
                    .bind_count = exp->let.bind_count,
                    .body       = close_exp(index + 1, exp->let.body, fvs, fv_count)
                }
            });
            FREE_BUF(closed_binds)
            return closed_exp;
        }
        case EXP_PI:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_PI,
                .type = close_exp(index, exp->type, fvs, fv_count),
                .pi   = {
                    .dom   = close_exp(index, exp->pi.dom, fvs, fv_count),
                    .codom = close_exp(index + 1, exp->pi.codom, fvs, fv_count)
                }
            });
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            ALLOC_BUF(closed_args, exp_t, exp->tup.arg_count)
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                closed_args[i] = close_exp(index, exp->tup.args[i], fvs, fv_count);
            exp_t closed_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = close_exp(index, exp->type, fvs, fv_count),
                .tup  = {
                    .args      = closed_args,
                    .arg_count = exp->tup.arg_count
                }
            });
            FREE_BUF(closed_args)
            return closed_exp;
        }
        case EXP_INJ:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_INJ,
                .type = close_exp(index, exp->type, fvs, fv_count),
                .inj  = {
                    .arg   = close_exp(index, exp->inj.arg, fvs, fv_count),
                    .index = exp->inj.index
                }
            });
        case EXP_ABS:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_ABS,
                .type = close_exp(index, exp->type, fvs, fv_count),
                .abs  = {
                    .pat  = exp->abs.pat,
                    .body = close_exp(index + 1, exp->abs.body, fvs, fv_count)
                }
            });
        case EXP_APP:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_APP,
                .type = close_exp(index, exp->type, fvs, fv_count),
                .app  = {
                    .left  = close_exp(index + 1, exp->app.left, fvs, fv_count),
                    .right = close_exp(index + 1, exp->app.right, fvs, fv_count)
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
        case EXP_BVAR:
        case EXP_LIT:
            return close_exp_type(index, exp, fvs, fv_count);
    }
}
