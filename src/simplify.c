#include <assert.h>
#include "exp.h"
#include "utils.h"

static inline bool is_let_rec(mod_t mod, exp_t* vars, exp_t* vals, size_t var_count) {
    // A let expression is recursive when some of its bindings
    // depend on some of its variables.
    bool rec = false;
    fvs_t fvs = new_fvs(mod, vars, var_count);
    for (size_t i = 0; i < var_count && !rec; ++i)
        rec |= contains_fvs(vals[i]->fvs, fvs);
    return rec;
}

static inline exp_t simplify_let(mod_t mod, exp_t let) {
    if (let->let.var_count == 0)
        return let->let.body;

    bool rec = let->tag == EXP_LETREC;
    size_t var_count = 0;
    NEW_BUF(vars, exp_t, let->let.var_count)
    NEW_BUF(vals, exp_t, let->let.var_count)
    for (size_t i = 0, n = let->let.var_count; i < n; ++i) {
        bool is_var_used = contains_fv(let->let.body->fvs, let->let.vars[i]);
        if (rec) {
            for (size_t j = 0; j < n && !is_var_used; ++j) {
                if (i == j) continue;
                is_var_used |= contains_fv(let->let.vals[j]->fvs, let->let.vars[i]);
            }
        }
        // If the body and other bindings do not depend on this variable,
        // we can then remove it from the binders.
        if (is_var_used) {
            vars[var_count] = let->let.vars[i];
            vals[var_count] = let->let.vals[i];
            var_count++;
        }
    }

    rec = rec && is_let_rec(mod, vars, vals, var_count);
    exp_t new_let = let;
    if (var_count != let->let.var_count || ((let->tag == EXP_LETREC) != rec))
        new_let = new_let_or_letrec(mod, rec, vars, vals, var_count, let->let.body, &let->loc);
    FREE_BUF(vars);
    FREE_BUF(vals);
    return new_let;
}

enum match_res {
    NO_MATCH,
    MATCH,
    MAY_MATCH
};

static inline enum match_res try_match(exp_t pat, exp_t arg, struct htable* map) {
    // Try to match the pattern against a value. If the match succeeds, return MATCH
    // with a map from pattern variable to value.
    switch (pat->tag) {
        case EXP_WILD: return MATCH;
        case EXP_LIT:  return arg == pat ? MATCH : (arg->tag == EXP_LIT ? NO_MATCH : MAY_MATCH);
        case EXP_VAR:
            insert_in_exp_map(map, pat, arg);
            return MATCH;
        case EXP_TUP:
            if (arg->tag == EXP_TUP) {
                assert(arg->tup.arg_count == pat->tup.arg_count);
                for (size_t i = 0, n = arg->tup.arg_count; i < n; ++i) {
                    enum match_res match_res = try_match(pat->tup.args[i], arg->tup.args[i], map);
                    if (match_res == NO_MATCH)
                        return NO_MATCH;
                    if (match_res == MAY_MATCH)
                        return MAY_MATCH;
                }
                return MATCH;
            }
            return MAY_MATCH;
        case EXP_INJ:
            if (arg->tag == EXP_INJ) {
                if (arg->inj.index != pat->inj.index)
                    return NO_MATCH;
                return try_match(pat->inj.arg, arg->inj.arg, map);
            }
            return MAY_MATCH;
        default:
            assert(false);
            return MAY_MATCH;
    }
}

static inline exp_t simplify_match(mod_t mod, exp_t match) {
    // Try to execute the match expression.
    struct htable map = new_exp_map();
    exp_t res = NULL;
    for (size_t i = 0, n = match->match.pat_count; i < n; ++i) {
        clear_htable(&map);
        enum match_res match_res = try_match(match->match.pats[i], match->match.arg, &map);
        switch (match_res) {
            case NO_MATCH:  continue;
            case MATCH:
                res = replace_exps(match->match.vals[i], &map);
                // fallthrough
            case MAY_MATCH:
                break;
        }
    }
    free_htable(&map);
    if (res)
        return res;

    // Remove patterns that are never going to match because they are
    // placed after a pattern that catches all possibilities.
    for (size_t i = 1, n = match->match.pat_count; i < n; ++i) {
        if (is_trivial_pat(match->match.pats[i - 1]))
            return new_match(mod, match->match.pats, match->match.vals, i, match->match.arg, &match->loc);
    }
    return match;
}

exp_t simplify_exp(mod_t mod, exp_t exp) {
    switch (exp->tag) {
        case EXP_PI:
            if (exp->pi.var && !contains_fv(exp->pi.codom->fvs, exp->pi.var))
                return new_pi(mod, NULL, exp->pi.dom, exp->pi.codom, &exp->loc);
            return exp;
        case EXP_LET:
        case EXP_LETREC:
            return simplify_let(mod, exp);
        case EXP_MATCH:
            return simplify_match(mod, exp);
        default:
            return exp;
    }
}