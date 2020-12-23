#include <assert.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/buf.h"
#include "utils/map.h"
#include "ir/exp.h"

// Ext -----------------------------------------------------------------------------

static inline exp_t simplify_ext(mod_t mod, exp_t ext) {
    if (ext->ext.val->tag == EXP_TUP) {
        assert(ext->ext.index->tag == EXP_LIT);
        assert(ext->ext.index->lit.int_val < ext->ext.val->tup.arg_count);
        return ext->ext.val->tup.args[ext->ext.index->lit.int_val];
    } else if (ext->ext.val->tag == EXP_INJ) {
        size_t index = ext->ext.index->lit.int_val;
        if (index == ext->ext.val->inj.index)
            return ext->ext.val->inj.arg;
        return new_bot(mod, ext->type, &ext->loc);
    }
    return ext;
}

// Ins -----------------------------------------------------------------------------

static inline exp_t simplify_ins(mod_t mod, exp_t ins) {
    if (ins->ins.val->tag == EXP_TUP) {
        assert(ins->ins.index->tag == EXP_LIT);
        exp_t* args = new_buf(exp_t, ins->ins.val->tup.arg_count);
        memcpy(args, ins->ins.val->tup.args, sizeof(exp_t) * ins->ins.val->tup.arg_count);
        assert(ins->ins.index->lit.int_val < ins->ins.val->tup.arg_count);
        args[ins->ins.index->lit.int_val] = ins->ins.elem;
        exp_t res = new_tup(mod, args, ins->ins.val->tup.arg_count, &ins->loc);
        free_buf(args);
        return res;
    } else if (ins->type->tag == EXP_SUM)
        return new_inj(mod, ins->type, ins->ins.index->lit.int_val, ins->ins.elem, &ins->loc);
    return ins;
}

// Let -----------------------------------------------------------------------------

static inline exp_t try_merge_let(mod_t mod, exp_t outer_let, exp_t inner_let) {
    // We can merge two let-expressions if the values of the inner one
    // do not reference the variables of the outer one.
    exp_t* inner_vars = new_buf(exp_t, outer_let->let.var_count + inner_let->let.var_count);
    exp_t* inner_vals = new_buf(exp_t, outer_let->let.var_count + inner_let->let.var_count);
    exp_t* outer_vars = new_buf(exp_t, outer_let->let.var_count);
    exp_t* outer_vals = new_buf(exp_t, outer_let->let.var_count);
    size_t inner_count = 0, outer_count = 0;
    for (size_t i = 0, n = outer_let->let.var_count; i < n; ++i) {
        bool push_down = true;
        for (size_t j = 0, m = inner_let->let.var_count; j < m && push_down; ++j)
            push_down &= !contains_var(inner_let->let.vals[j]->free_vars, outer_let->let.vars[i]);
        if (push_down) {
            inner_vars[inner_count] = outer_let->let.vars[i];
            inner_vals[inner_count] = outer_let->let.vals[i];
            inner_count++;
        } else {
            outer_vars[outer_count] = outer_let->let.vars[i];
            outer_vals[outer_count] = outer_let->let.vals[i];
            outer_count++;
        }
    }
    if (outer_count != outer_let->let.var_count) {
        memcpy(inner_vals + inner_count, inner_let->let.vals, sizeof(exp_t) * inner_let->let.var_count);
        memcpy(inner_vars + inner_count, inner_let->let.vars, sizeof(exp_t) * inner_let->let.var_count);
        inner_count += inner_let->let.var_count;
        inner_let = new_let(mod, inner_vars, inner_vals, inner_count, inner_let->let.body, &inner_let->loc);
        outer_let = new_let(mod, outer_vars, outer_vals, outer_count, inner_let, &outer_let->loc);
    } else
        outer_let = NULL;
    free_buf(inner_vars);
    free_buf(inner_vals);
    free_buf(outer_vars);
    free_buf(outer_vals);
    return outer_let;
}

static inline exp_t simplify_let(mod_t mod, exp_t let) {
    if (let->let.var_count == 0)
        return let->let.body;

    if (let->let.body->tag == EXP_LET) {
        exp_t res;
        if ((res = try_merge_let(mod, let, let->let.body)))
            return res;
    }

    size_t var_count = 0;
    exp_t* vars = new_buf(exp_t, let->let.var_count);
    exp_t* vals = new_buf(exp_t, let->let.var_count);
    for (size_t i = 0, n = let->let.var_count; i < n; ++i) {
        // Only keep the variables that are referenced in the body
        if (contains_var(let->let.body->free_vars, let->let.vars[i])) {
            vars[var_count] = let->let.vars[i];
            vals[var_count] = let->let.vals[i];
            var_count++;
        }
    }

    exp_t res = var_count != let->let.var_count
        ? new_let(mod, vars, vals, var_count, let->let.body, &let->loc)
        : let;
    free_buf(vars);
    free_buf(vals);
    return res;
}

// Letrec --------------------------------------------------------------------------

struct var_binding {
    exp_t  val;
    vars_t uses;
};

MAP(bindings, exp_t, struct var_binding)

static exp_t split_letrec_var(mod_t, exp_t, exp_t, exp_t, struct exp_set*, struct bindings*);

static inline exp_t split_letrec_vars(
    mod_t mod, exp_t body, exp_t letrec, vars_t vars,
    struct exp_set* done, struct bindings* bindings)
{
    for (size_t i = 0, n = vars->count; i < n; ++i)
        body = split_letrec_var(mod, body, letrec, vars->vars[i], done, bindings);
    return body;
}

static exp_t split_letrec_var(
    mod_t mod, exp_t body, exp_t letrec, exp_t var,
    struct exp_set* done, struct bindings* bindings)
{
    if (!insert_in_exp_set(done, var))
        return body;
    struct var_binding* binding = find_in_bindings(bindings, var);
    if (contains_var(binding->uses, var)) {
        // If this binding is recursive, find all the members
        // of the cycle and group them together in a letrec.
        exp_t* rec_vars = new_buf(exp_t, binding->uses->count);
        exp_t* rec_vals = new_buf(exp_t, binding->uses->count);
        size_t rec_count = 1;
        rec_vars[0] = var;
        rec_vals[0] = binding->val;
        for (size_t i = 0, n = binding->uses->count; i < n; ++i) {
            exp_t other_var = binding->uses->vars[i];
            if (other_var == var)
                continue;
            struct var_binding* other_binding = find_in_bindings(bindings, other_var);
            if (contains_var(other_binding->uses, var) && insert_in_exp_set(done, other_var)) {
                rec_vars[rec_count] = other_var;
                rec_vals[rec_count] = other_binding->val;
                rec_count++;
            }
        }
        if (letrec->letrec.var_count != rec_count) {
            body = split_letrec_vars(mod, body, letrec, binding->uses, done, bindings);
            // Generate a letrec-expression for the cycle
            body = new_letrec(mod, rec_vars, rec_vals, rec_count, body, &letrec->loc);
        } else
            body = letrec;
        free_buf(rec_vars);
        free_buf(rec_vals);
    } else {
        body = split_letrec_vars(mod, body, letrec, binding->uses, done, bindings);
        // Generate a non-recursive let-expression for this variable
        body = new_let(mod, &var, (exp_t*)&binding->val, 1, body, &letrec->loc);
    }
    return body;
}

static inline vars_t transitive_uses(mod_t mod, vars_t uses, struct bindings* bindings) {
    vars_t old_uses = uses;
    for (size_t j = 0, m = old_uses->count; j < m; ++j)
        uses = union_vars(mod, uses, find_in_bindings(bindings, old_uses->vars[j])->uses);
    return uses;
}

static inline exp_t simplify_letrec(mod_t mod, exp_t letrec) {
    struct bindings bindings = new_bindings();

    // Create initial bindings with empty uses
    for (size_t i = 0, n = letrec->letrec.var_count; i < n; ++i) {
        insert_in_bindings(&bindings,
            letrec->letrec.vars[i],
            (struct var_binding) {
                .val = letrec->letrec.vals[i],
                .uses = new_vars(mod, NULL, 0)
            });
    }
    vars_t letrec_vars = new_vars(mod, letrec->letrec.vars, letrec->letrec.var_count);

    // We start by getting the direct uses (i.e. for a given variable, the variables of the
    // letrec-expression that use it in their definition). For the letrec-expression
    // (letrec ((#1 : nat) (#2 : nat) (#3 : nat)) (#2 #3 #1) #1)
    // we get:
    // #1 "used by" { #3 }
    // #2 "used by" { #1 }
    // #3 "used by" { #2 }
    for (size_t i = 0, n = letrec->letrec.var_count; i < n; ++i) {
        vars_t used_vars = intr_vars(mod, letrec->letrec.vals[i]->free_vars, letrec_vars);
        for (size_t j = 0, m = used_vars->count; j < m; ++j) {
            struct var_binding* binding = find_in_bindings(&bindings, used_vars->vars[j]);
            binding->uses = union_vars(mod, binding->uses, new_vars(mod, &letrec->letrec.vars[i], 1));
        }
    }

    // For each binding, add the uses of its uses. This is basically
    // computing the fix point of the transitive_uses() function.
    // The result is a map from variable to the variables that use it.
    // For the letrec-expression above, we would get at the end of the process:
    // #1 "used by transitively" { #1, #2, #3 }
    // #2 "used by transitively" { #1, #2, #3 }
    // #3 "used by transitively" { #1, #2, #3 }
    bool todo;
    do {
        todo = false;
        FORALL_IN_MAP(&bindings, exp_t, key, struct var_binding, binding, {
            vars_t uses = transitive_uses(mod, binding->uses, &bindings);
            todo |= binding->uses != uses;
            binding->uses = uses;
        })
    } while(todo);

    // We need to compute the variables that are needed (transitively) to compute the body.
    vars_t body_vars = intr_vars(mod, letrec->letrec.body->free_vars, letrec_vars);
    do {
        vars_t old_vars = body_vars;
        for (size_t i = 0, n = old_vars->count; i < n; ++i) {
            body_vars = union_vars(mod, body_vars,
                intr_vars(mod, find_in_bindings(&bindings, old_vars->vars[i])->val->free_vars, letrec_vars));
        }
        todo = body_vars != old_vars;
    } while (todo);

    // Now, we can simplify the letrec expression, by breaking individual cycles into
    // several letrec-expressions and separating non-recursive bindings into distinct,
    // regular (non-recursive) let-expressions.
    struct exp_set done = new_exp_set();
    exp_t res = split_letrec_vars(mod, letrec->letrec.body, letrec, body_vars, &done, &bindings);
    free_bindings(&bindings);
    free_exp_set(&done);
    return res;
}

// Match ---------------------------------------------------------------------------

enum match_res {
    NO_MATCH, MATCH, MAY_MATCH
};

static inline bool is_reduced(exp_t exp) {
    return
        exp->tag != EXP_VAR &&
        exp->tag != EXP_APP &&
        exp->tag != EXP_LET &&
        exp->tag != EXP_LETREC &&
        exp->tag != EXP_MATCH;
}

static inline enum match_res try_match(exp_t pat, exp_t arg, struct exp_map* map) {
    // Try to match the pattern against a value. If the match succeeds, return MATCH
    // and record the value associated with each pattern variable in the map.
    switch (pat->tag) {
        case EXP_LIT:  return arg == pat ? MATCH : (is_reduced(arg) ? NO_MATCH : MAY_MATCH);
        case EXP_VAR:
            if (!is_unbound_var(pat))
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
            return is_reduced(arg) ? NO_MATCH : MAY_MATCH;
        case EXP_INJ:
            if (arg->tag == EXP_INJ) {
                if (arg->inj.index != pat->inj.index)
                    return NO_MATCH;
                return try_match(pat->inj.arg, arg->inj.arg, map);
            }
            return is_reduced(arg) ? NO_MATCH : MAY_MATCH;
        default:
            assert(false && "invalid pattern");
            return MAY_MATCH;
    }
}

static inline exp_t simplify_match(mod_t mod, exp_t match) {
    // Try to execute the match expression.
    struct exp_map map = new_exp_map();
    exp_t res = NULL;
    for (size_t i = 0, n = match->match.pat_count; i < n; ++i) {
        clear_exp_map(&map);
        enum match_res match_res = try_match(match->match.pats[i], match->match.arg, &map);
        switch (match_res) {
            case NO_MATCH:
                // If all the cases are guaranteed not to match the argument,
                // return a bottom value.
                if (i == n - 1)
                    res = new_bot(mod, match->type, &match->loc);
                continue;
            case MATCH:
                res = replace_exps(match->match.vals[i], &map);
                // fallthrough
            case MAY_MATCH:
                break;
        }
    }
    free_exp_map(&map);
    // If the match expression could be executed, return the result
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

// Simplify ------------------------------------------------------------------------

exp_t simplify_exp(mod_t mod, exp_t exp) {
    switch (exp->tag) {
        case EXP_INS:
            return simplify_ins(mod, exp);
        case EXP_EXT:
            return simplify_ext(mod, exp);
        case EXP_LET:
            return simplify_let(mod, exp);
        case EXP_ARROW:
            if (!is_unbound_var(exp->arrow.var) && !contains_var(exp->arrow.codom->free_vars, exp->arrow.var))
                return new_arrow(mod, new_unbound_var(mod, exp->arrow.var->type, &exp->arrow.var->loc), exp->arrow.codom, &exp->loc);
            return exp;
        case EXP_ABS:
            if (!is_unbound_var(exp->abs.var) && !contains_var(exp->abs.body->free_vars, exp->abs.var))
                return new_abs(mod, new_unbound_var(mod, exp->abs.var->type, &exp->abs.var->loc), exp->abs.body, &exp->loc);
            return exp;
        case EXP_BOT:
        case EXP_TOP:
            if (exp->type->tag == EXP_PROD) {
                exp_t* args = new_buf(exp_t, exp->type->prod.arg_count);
                for (size_t i = 0, n = exp->type->prod.arg_count; i < n; ++i) {
                    args[i] = exp->tag == EXP_TOP
                        ? new_top(mod, exp->type->prod.args[i], &exp->loc)
                        : new_bot(mod, exp->type->prod.args[i], &exp->loc);
                }
                exp_t res = new_tup(mod, args, exp->type->prod.arg_count, &exp->loc);
                free_buf(args);
                return res;
            }
            return exp;
        case EXP_LETREC:
            return simplify_letrec(mod, exp);
        case EXP_MATCH:
            return simplify_match(mod, exp);
        default:
            return exp;
    }
}
