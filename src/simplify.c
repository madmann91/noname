#include <assert.h>
#include <string.h>
#include "exp.h"
#include "utils.h"
#include "hash.h"

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
        NEW_BUF(args, exp_t, ins->ins.val->tup.arg_count)
        memcpy(args, ins->ins.val->tup.args, sizeof(exp_t) * ins->ins.val->tup.arg_count);
        assert(ins->ins.index->lit.int_val < ins->ins.val->tup.arg_count);
        args[ins->ins.index->lit.int_val] = ins->ins.elem;
        exp_t res = new_tup(mod, args, ins->ins.val->tup.arg_count, &ins->loc);
        FREE_BUF(args);
        return res;
    } else if (ins->type->tag == EXP_SUM)
        return new_inj(mod, ins->type, ins->ins.index->lit.int_val, ins->ins.elem, &ins->loc);
    return ins;
}

// Let -----------------------------------------------------------------------------

static inline exp_t try_merge_let(mod_t mod, exp_t outer_let, exp_t inner_let) {
    // We can merge two let-expressions if the values of the inner one
    // do not reference the variables of the outer one.
    NEW_BUF(inner_vars, exp_t, outer_let->let.var_count + inner_let->let.var_count)
    NEW_BUF(inner_vals, exp_t, outer_let->let.var_count + inner_let->let.var_count)
    NEW_BUF(outer_vars, exp_t, outer_let->let.var_count)
    NEW_BUF(outer_vals, exp_t, outer_let->let.var_count)
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
    FREE_BUF(inner_vars);
    FREE_BUF(inner_vals);
    FREE_BUF(outer_vars);
    FREE_BUF(outer_vals);
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
    NEW_BUF(vars, exp_t, let->let.var_count)
    NEW_BUF(vals, exp_t, let->let.var_count)
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
    FREE_BUF(vars);
    FREE_BUF(vals);
    return res;
}

// Letrec --------------------------------------------------------------------------

struct binding {
    exp_t var, val;
    vars_t uses;
};

static bool cmp_binding(const void* ptr1, const void* ptr2) {
    return ((struct binding*)ptr1)->var == ((struct binding*)ptr2)->var;
}

static inline struct htable new_bindings(void) {
    return new_htable(sizeof(struct binding), DEFAULT_CAP, cmp_binding);
}

static inline struct binding* find_binding(struct htable* bindings, exp_t var) {
    return find_in_htable(bindings, &var, hash_ptr(FNV_OFFSET, var));
}

static inline bool insert_binding(struct htable* bindings, exp_t var, exp_t val, vars_t uses) {
    return insert_in_htable(bindings,
        &(struct binding) { .var = var, .val = val, .uses = uses },
        hash_ptr(FNV_OFFSET, var), NULL);
}

static exp_t split_letrec_var(mod_t, exp_t, exp_t, exp_t, struct htable*, struct htable*);

static inline exp_t split_letrec_vars(
    mod_t mod, exp_t body, exp_t letrec, vars_t vars,
    struct htable* done, struct htable* bindings)
{
    for (size_t i = 0, n = vars->count; i < n; ++i)
        body = split_letrec_var(mod, body, letrec, vars->vars[i], done, bindings);
    return body;
}

static exp_t split_letrec_var(mod_t mod, exp_t body, exp_t letrec, exp_t var, struct htable* done, struct htable* bindings) {
    if (!insert_in_exp_set(done, var))
        return body;
    struct binding* binding = find_binding(bindings, var);
    if (contains_var(binding->uses, var)) {
        // If this binding is recursive, find all the members
        // of the cycle and group them together in a letrec.
        NEW_BUF(rec_vars, exp_t, binding->uses->count)
        NEW_BUF(rec_vals, exp_t, binding->uses->count)
        size_t rec_count = 1;
        rec_vars[0] = var;
        rec_vals[0] = binding->val;
        for (size_t i = 0, n = binding->uses->count; i < n; ++i) {
            exp_t other_var = binding->uses->vars[i];
            if (other_var == var)
                continue;
            struct binding* other_binding = find_binding(bindings, other_var);
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
        FREE_BUF(rec_vars);
        FREE_BUF(rec_vals);
    } else {
        body = split_letrec_vars(mod, body, letrec, binding->uses, done, bindings);
        // Generate a non-recursive let-expression for this variable
        body = new_let(mod, &var, (exp_t*)&binding->val, 1, body, &letrec->loc);
    }
    return body;
}

static inline vars_t transitive_uses(mod_t mod, vars_t uses, struct htable* bindings) {
    vars_t old_uses = uses;
    for (size_t j = 0, m = old_uses->count; j < m; ++j)
        uses = union_vars(mod, uses, find_binding(bindings, old_uses->vars[j])->uses);
    return uses;
}

static inline exp_t simplify_letrec(mod_t mod, exp_t letrec) {
    struct htable bindings = new_bindings();

    // Create initial bindings with empty uses
    for (size_t i = 0, n = letrec->letrec.var_count; i < n; ++i) {
        insert_binding(&bindings,
            letrec->letrec.vars[i],
            letrec->letrec.vals[i],
            new_vars(mod, NULL, 0));
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
            struct binding* binding = find_binding(&bindings, used_vars->vars[j]);
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
        for (size_t i = 0, n = bindings.elem_cap; i < n; ++i) {
            if (is_elem_deleted(bindings.hashes[i]))
                continue;
            struct binding* binding = &((struct binding*)bindings.elems)[i];
            vars_t uses = transitive_uses(mod, binding->uses, &bindings);
            todo |= binding->uses != uses;
            binding->uses = uses;
        }
    } while(todo);

    // We need to compute the variables that are needed (transitively) to compute the body.
    vars_t body_vars = intr_vars(mod, letrec->letrec.body->free_vars, letrec_vars);
    do {
        vars_t old_vars = body_vars;
        for (size_t i = 0, n = old_vars->count; i < n; ++i) {
            body_vars = union_vars(mod, body_vars,
                intr_vars(mod, find_binding(&bindings, old_vars->vars[i])->val->free_vars, letrec_vars));
        }
        todo = body_vars != old_vars;
    } while (todo);

    // Now, we can simplify the letrec expression, by breaking individual cycles into
    // several letrec-expressions and separating non-recursive bindings into distinct,
    // regular (non-recursive) let-expressions.
    struct htable done = new_exp_set();
    exp_t res = split_letrec_vars(mod, letrec->letrec.body, letrec, body_vars, &done, &bindings);
    free_htable(&bindings);
    free_htable(&done);
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

static inline enum match_res try_match(exp_t pat, exp_t arg, struct htable* map) {
    // Try to match the pattern against a value. If the match succeeds, return MATCH
    // and record the value associated with each pattern variable in the map.
    switch (pat->tag) {
        case EXP_WILD: return MATCH;
        case EXP_LIT:  return arg == pat ? MATCH : (is_reduced(arg) ? NO_MATCH : MAY_MATCH);
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
            return is_reduced(arg) ? NO_MATCH : MAY_MATCH;
        case EXP_INJ:
            if (arg->tag == EXP_INJ) {
                if (arg->inj.index != pat->inj.index)
                    return NO_MATCH;
                return try_match(pat->inj.arg, arg->inj.arg, map);
            }
            return is_reduced(arg) ? NO_MATCH : MAY_MATCH;
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
    free_htable(&map);
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
        case EXP_PI:
            if (exp->pi.var && !contains_var(exp->pi.codom->free_vars, exp->pi.var))
                return new_pi(mod, NULL, exp->pi.dom, exp->pi.codom, &exp->loc);
            return exp;
        case EXP_LET:
            return simplify_let(mod, exp);
        case EXP_BOT:
        case EXP_TOP:
            if (exp->type->tag == EXP_PROD) {
                NEW_BUF(args, exp_t, exp->type->prod.arg_count)
                for (size_t i = 0, n = exp->type->prod.arg_count; i < n; ++i) {
                    args[i] = exp->tag == EXP_TOP
                        ? new_top(mod, exp->type->prod.args[i], &exp->loc)
                        : new_bot(mod, exp->type->prod.args[i], &exp->loc);
                }
                exp_t res = new_tup(mod, args, exp->type->prod.arg_count, &exp->loc);
                FREE_BUF(args);
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
