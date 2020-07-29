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
            ALLOC_BUF(new_binds, exp_t, exp->let.bind_count)
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
            ALLOC_BUF(new_args, exp_t, exp->tup.arg_count)
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
                .abs  = {
                    .pat  = exp->abs.pat,
                    .body = open_or_close_exp(open, index + 1, exp->abs.body, fvs, fv_count)
                }
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
