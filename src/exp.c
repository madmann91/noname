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

static inline exp_t open_exp_type(size_t index, exp_t exp, exp_t* fvs) {
    exp_t opened_type = open_exp(index, exp->type, fvs);
    if (opened_type == exp->type)
        return exp;
    struct exp copy = *exp;
    copy.type = opened_type;
    return rebuild_exp(&copy);
}

exp_t open_exp(size_t index, exp_t exp, exp_t* fvs) {
    switch (exp->tag) {
        case EXP_BVAR:
            if (exp->bvar.index == index)
                return fvs[exp->bvar.sub_index];
            return open_exp_type(index, exp, fvs);
        case EXP_LET: {
            ALLOC_BUF(opened_binds, exp_t, exp->let.bind_count)
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i) {
                opened_binds[i] = open_exp(
                    index + 1,
                    exp->let.binds[i],
                    fvs);
            }
            exp_t opened_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = open_exp(index, exp->type, fvs),
                .let  = {
                    .binds      = opened_binds,
                    .bind_count = exp->let.bind_count,
                    .body       = open_exp(index + 1, exp->let.body, fvs)
                }
            });
            FREE_BUF(opened_binds)
            return opened_exp;
        }
        case EXP_PI:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_PI,
                .type = open_exp(index, exp->type, fvs),
                .pi   = {
                    .dom   = open_exp(index, exp->pi.dom, fvs),
                    .codom = open_exp(index + 1, exp->pi.codom, fvs)
                }
            });
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            ALLOC_BUF(opened_args, exp_t, exp->tup.arg_count)
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                opened_args[i] = open_exp(index, exp->tup.args[i], fvs);
            exp_t opened_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = open_exp(index, exp->type, fvs),
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
                .type = open_exp(index, exp->type, fvs),
                .inj  = {
                    .arg   = open_exp(index, exp->inj.arg, fvs),
                    .index = exp->inj.index
                }
            });
        case EXP_ABS:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_ABS,
                .type = open_exp(index, exp->type, fvs),
                .abs  = {
                    .pat  = exp->abs.pat,
                    .body = open_exp(index + 1, exp->abs.body, fvs)
                }
            });
        case EXP_APP:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_APP,
                .type = open_exp(index, exp->type, fvs),
                .app  = {
                    .left  = open_exp(index + 1, exp->app.left, fvs),
                    .right = open_exp(index + 1, exp->app.right, fvs)
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
            return open_exp_type(index, exp, fvs);
    }
}

exp_t close_exp(size_t index, exp_t exp, exp_t* fvs) {

}
