#include <assert.h>
#include "check.h"
#include "env.h"
#include "format.h"
#include "utils.h"

struct checker {
    struct log* log;
    env_t env;
};

checker_t new_checker(struct log* log) {
    checker_t checker = xmalloc(sizeof(struct checker));
    checker->log = log;
    checker->env = new_env();
    return checker;
}

void free_checker(checker_t checker) {
    free_env(checker->env);
    free(checker);
}

static inline size_t get_exp_level(exp_t exp) {
    size_t level = 0;
    while (exp) {
        exp = exp->type;
        level++;
    }
    return level;
}

static inline exp_t get_min_level_exp(exp_t* exps, size_t n) {
    size_t min_level = SIZE_MAX;
    exp_t exp = NULL;
    for (size_t i = 0; i < n; ++i) {
        size_t level = get_exp_level(exps[i]);
        if (level < min_level) {
            exp = exps[i];
            min_level = level;
        }
    }
    return exp;
}

static inline bool expect(checker_t checker, const struct loc* loc, exp_t type, const char* expected) {
    log_error(
        checker->log, loc,
        "expected type '%0:$%1:s%2:$', but got '%3:e'",
        FMT_ARGS(
            { .style = STYLE_KEYWORD },
            { .s = expected },
            { .style = 0 },
            { .e = type }));
    return false;
}

static inline bool expect_type(checker_t checker, const struct loc* loc, exp_t type, exp_t expected) {
    if (type != expected) {
        log_error(
            checker->log, loc,
            "expected type '%0:e', but got '%1:e'",   
            FMT_ARGS({ .e = expected }, { .e = type }));
        return false;
    }
    return true;
}

bool check_exp(checker_t checker, exp_t exp) {
    if (exp->type)
        check_exp(checker, exp->type);
    else if (exp->tag == EXP_UNI)
        return true;
    else {
        log_error(checker->log, &exp->loc, "expression without a type", NULL);
        return false;
    }
    switch (exp->tag) {
        case EXP_BVAR: {
            exp_t type = get_exp_from_env(checker->env, exp->bvar.index, exp->bvar.sub_index);
            if (!type) {
                log_error(
                    checker->log, &exp->loc, "invalid De Bruijn index #%0:u.%1:u",
                    FMT_ARGS({ .u = exp->bvar.index }, { .u = exp->bvar.sub_index }));
                return false;
            }
            return expect_type(checker, &exp->loc, exp->type, type);
        }
        case EXP_FVAR:
            return true;
        case EXP_ABS:
            if (exp->type->tag != EXP_PI)
                return expect(checker, &exp->loc, exp->type, "pi");
            return expect_type(checker, &exp->abs.body->loc, exp->abs.body->type, exp->type->pi.codom);
        case EXP_STAR:
            if (exp->type->tag != EXP_UNI)
                return expect(checker, &exp->loc, exp->type, "uni");
            return true;
        case EXP_NAT:
        case EXP_INT:
        case EXP_REAL:
            if (exp->type->tag != EXP_STAR)
                return expect(checker, &exp->loc, exp->type, "star");
            if (exp->tag == EXP_INT || exp->tag == EXP_REAL)
                return expect(checker, &exp->real.bitwidth->loc, exp->real.bitwidth->type, "nat");
            return true;
        case EXP_PI:
            return expect_type(checker, &exp->loc, exp->type, shift_exp(0, exp->pi.codom->type, 1, false));
        case EXP_LIT:
            // Any literal with a type different from those is a bug, not a type error
            assert(
                exp->type->tag == EXP_NAT ||
                exp->type->tag == EXP_REAL ||
                exp->type->tag == EXP_INT);
            return true;
        case EXP_LET: {
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i) {
                if (!check_exp(checker, exp->let.binds[i]))
                    return false;
            }
            exp_t body_type =
                shift_exp(0,
                    open_exp(0, exp->let.body->type, exp->let.binds, exp->let.bind_count),
                    1, false);
            if (!expect_type(checker, &exp->let.body->loc, body_type, exp->type))
                return false;
            push_env_level(checker->env);
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i)
                add_exp_to_env(checker->env, exp->let.binds[i]->type);
            bool status = check_exp(checker, exp->let.body);
            pop_env_level(checker->env);
            return status;
        }
        case EXP_WILD:
            add_exp_to_env(checker->env, exp->type);
            return true;
        case EXP_MATCH: {
            if (!check_exp(checker, exp->match.arg))
                return false;
            exp_t val_type = shift_exp(0, exp->type, 1, false);
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                size_t exp_count = get_env_size(checker->env);
                exp_t pat = exp->match.pats[i];
                exp_t val = exp->match.vals[i];
                bool ok = check_exp(checker, pat);
                push_env_level_from(checker->env, exp_count);
                ok = ok && expect_type(checker, &pat->loc, pat->type, exp->match.arg->type);
                ok = ok && check_exp(checker, exp) && expect_type(checker, &exp->loc, val->type, val_type);
                pop_env_level(checker->env);
                if (!ok)
                    return false;
            }
            return true;
        }
        case EXP_SUM:
        case EXP_PROD: {
            exp_t type = get_min_level_exp(exp->sum.args, exp->sum.arg_count)->type;
            return expect_type(checker, &exp->loc, exp->type, type);
        }
        case EXP_TUP: {
            if (exp->type->tag != EXP_PROD)
                return expect(checker, &exp->loc, exp->type, "prod");
            if (exp->type->prod.arg_count != exp->tup.arg_count)
                log_error(checker->log, &exp->loc, "invalid number of arguments", NULL);
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i) {
                exp_t arg = exp->tup.args[i];
                if (!check_exp(checker, arg) ||
                    !expect_type(checker, &arg->loc, arg->type, exp->type->prod.args[i]))
                    return false;
            }
            return true;
        }
        default:
            assert(false);
            return false;
    }
}
