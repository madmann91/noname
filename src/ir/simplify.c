#include <assert.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/buf.h"
#include "utils/map.h"
#include "ir/node.h"

// Ext -----------------------------------------------------------------------------

static inline node_t simplify_ext(mod_t mod, node_t ext) {
    if (ext->ext.val->tag == NODE_RECORD) {
        size_t index = find_label_in_node(ext->ext.val, ext->ext.label);
        assert(index != SIZE_MAX);
        return ext->ext.val->record.args[index];
    } else if (ext->ext.val->tag == NODE_INJ) {
        return ext->ext.val->inj.label == ext->ext.label
            ? ext->ext.val->inj.arg
            : new_bot(mod, ext->type, &ext->loc);
    }
    return ext;
}

// Ins -----------------------------------------------------------------------------

static inline node_t simplify_ins(mod_t mod, node_t ins) {
    if (ins->ins.val->tag == NODE_RECORD) {
        node_t* args = new_buf(node_t, ins->ins.val->record.arg_count);
        memcpy(args, ins->ins.val->record.args, sizeof(node_t) * ins->ins.val->record.arg_count);
        size_t index = find_label_in_node(ins->ins.val, ins->ins.label);
        assert(index != SIZE_MAX);
        args[index] = ins->ins.elem;
        node_t res = new_record(mod, args, ins->ins.val->record.labels, ins->ins.val->record.arg_count, &ins->loc);
        free_buf(args);
        return res;
    } else if (ins->type->tag == NODE_SUM) {
        return new_inj(mod, ins->type, ins->ins.label, ins->ins.elem, &ins->loc);
    }
    return ins;
}

// Tup -----------------------------------------------------------------------------

static inline node_t simplify_record(node_t record) {
    // Turn records that are made of individual extracts into their source tuple
    node_t from = NULL;
    for (size_t i = 0, n = record->record.arg_count; i < n; ++i) {
        if (record->record.args[i]->tag == NODE_EXT &&
            record->record.args[i]->ext.label == record->record.labels[i] &&
            (!from || from == record->record.args[i]->ext.val))
            from = record->record.args[i]->ext.val;
        else {
            from = NULL;
            break;
        }
    }
    return from && from->type == record->type ? from : record;
}

// Let -----------------------------------------------------------------------------

static inline node_t try_merge_let(mod_t mod, node_t outer_let, node_t inner_let) {
    // We can merge two let-noderessions if the values of the inner one
    // do not reference the variables of the outer one.
    node_t* inner_vars = new_buf(node_t, outer_let->let.var_count + inner_let->let.var_count);
    node_t* inner_vals = new_buf(node_t, outer_let->let.var_count + inner_let->let.var_count);
    node_t* outer_vars = new_buf(node_t, outer_let->let.var_count);
    node_t* outer_vals = new_buf(node_t, outer_let->let.var_count);
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
        memcpy(inner_vals + inner_count, inner_let->let.vals, sizeof(node_t) * inner_let->let.var_count);
        memcpy(inner_vars + inner_count, inner_let->let.vars, sizeof(node_t) * inner_let->let.var_count);
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

static inline node_t simplify_let(mod_t mod, node_t let) {
    if (let->let.var_count == 0)
        return let->let.body;

    if (let->let.body->tag == NODE_LET) {
        node_t res;
        if ((res = try_merge_let(mod, let, let->let.body)))
            return res;
    }

    size_t var_count = 0;
    node_t* vars = new_buf(node_t, let->let.var_count);
    node_t* vals = new_buf(node_t, let->let.var_count);
    node_t body = let->let.body;
    for (size_t i = 0, n = let->let.var_count; i < n; ++i) {
        // Only keep the variables that are referenced in the body
        if (contains_var(body->free_vars, let->let.vars[i])) {
            // Remove variables that are directly equal to another
            if (let->let.vals[i]->tag == NODE_VAR) {
                body = replace_var(body, let->let.vars[i], let->let.vals[i]);
            } else {
                vars[var_count] = let->let.vars[i];
                vals[var_count] = let->let.vals[i];
                var_count++;
            }
        }
    }

    node_t res = var_count != let->let.var_count
        ? new_let(mod, vars, vals, var_count, body, &let->loc)
        : let;
    free_buf(vars);
    free_buf(vals);
    return res;
}

// Letrec --------------------------------------------------------------------------

struct var_binding {
    node_t  val;
    vars_t uses;
};

MAP(bindings, node_t, struct var_binding)

static node_t split_letrec_var(mod_t, node_t, node_t, node_t, struct node_set*, struct bindings*);

static inline node_t split_letrec_vars(
    mod_t mod, node_t body, node_t letrec, vars_t vars,
    struct node_set* done, struct bindings* bindings)
{
    for (size_t i = 0, n = vars->count; i < n; ++i)
        body = split_letrec_var(mod, body, letrec, vars->vars[i], done, bindings);
    return body;
}

static node_t split_letrec_var(
    mod_t mod, node_t body, node_t letrec, node_t var,
    struct node_set* done, struct bindings* bindings)
{
    if (!insert_in_node_set(done, var))
        return body;
    struct var_binding* binding = find_in_bindings(bindings, var);
    if (contains_var(binding->uses, var)) {
        // If this binding is recursive, find all the members
        // of the cycle and group them together in a letrec.
        node_t* rec_vars = new_buf(node_t, binding->uses->count);
        node_t* rec_vals = new_buf(node_t, binding->uses->count);
        size_t rec_count = 1;
        rec_vars[0] = var;
        rec_vals[0] = binding->val;
        for (size_t i = 0, n = binding->uses->count; i < n; ++i) {
            node_t other_var = binding->uses->vars[i];
            if (other_var == var)
                continue;
            struct var_binding* other_binding = find_in_bindings(bindings, other_var);
            if (contains_var(other_binding->uses, var) && insert_in_node_set(done, other_var)) {
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
        body = new_let(mod, &var, (node_t*)&binding->val, 1, body, &letrec->loc);
    }
    return body;
}

static inline vars_t transitive_uses(mod_t mod, vars_t uses, struct bindings* bindings) {
    vars_t old_uses = uses;
    for (size_t j = 0, m = old_uses->count; j < m; ++j)
        uses = union_vars(mod, uses, find_in_bindings(bindings, old_uses->vars[j])->uses);
    return uses;
}

static inline node_t simplify_letrec(mod_t mod, node_t letrec) {
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
        FORALL_IN_MAP(&bindings, node_t, key, struct var_binding, binding, {
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
    struct node_set done = new_node_set();
    node_t res = split_letrec_vars(mod, letrec->letrec.body, letrec, body_vars, &done, &bindings);
    free_bindings(&bindings);
    free_node_set(&done);
    return res;
}

// Match ---------------------------------------------------------------------------

enum match_res {
    NO_MATCH, MATCH, MAY_MATCH
};

static inline enum match_res try_match(mod_t mod, node_t pat, node_t arg, struct node_vec* vars, struct node_vec* vals) {
    // Try to match the pattern against a value. If the match succeeds, return MATCH
    // and record the value associated with each pattern variable in the map.
    switch (pat->tag) {
        case NODE_LIT:
            return arg == pat ? MATCH : (arg->tag == NODE_LIT ? NO_MATCH : MAY_MATCH);
        case NODE_VAR:
            if (!is_unbound_var(pat)) {
                push_to_node_vec(vars, pat);
                push_to_node_vec(vals, arg);
            }
            return MATCH;
        case NODE_RECORD:
            assert(arg->type->tag == NODE_PROD && arg->type->prod.arg_count == pat->record.arg_count);
            for (size_t i = 0, n = pat->record.arg_count; i < n; ++i) {
                node_t elem = new_ext(mod, arg, pat->record.labels[i], &pat->record.args[i]->loc);
                enum match_res match_res = try_match(mod, pat->record.args[i], elem, vars, vals);
                if (match_res == NO_MATCH)
                    return NO_MATCH;
                if (match_res == MAY_MATCH)
                    return MAY_MATCH;
            }
            return MATCH;
        case NODE_INJ:
            if (arg->tag == NODE_INJ) {
                if (arg->inj.label != pat->inj.label)
                    return NO_MATCH;
                return try_match(mod, pat->inj.arg, arg->inj.arg, vars, vals);
            }
            return MAY_MATCH;
        default:
            assert(false && "invalid pattern");
            return MAY_MATCH;
    }
}

static inline node_t simplify_match(mod_t mod, node_t match) {
    // Try to execute the match expression.
    node_t vars_buf[16];
    node_t vals_buf[16];
    struct node_vec vars = new_node_vec_on_stack(ARRAY_SIZE(vars_buf), vars_buf);
    struct node_vec vals = new_node_vec_on_stack(ARRAY_SIZE(vals_buf), vals_buf);
    node_t res = NULL;
    for (size_t i = 0, n = match->match.pat_count; i < n; ++i) {
        clear_node_vec(&vars);
        clear_node_vec(&vals);
        enum match_res match_res = try_match(mod, match->match.pats[i], match->match.arg, &vars, &vals);
        assert(vars.size == vals.size);
        switch (match_res) {
            case NO_MATCH:
                // If all the cases are guaranteed not to match the argument,
                // return a bottom value.
                if (i == n - 1)
                    res = new_bot(mod, match->type, &match->loc);
                continue;
            case MATCH:
                res = replace_vars(match->match.vals[i], vars.elems, vals.elems, vars.size);
                // fallthrough
            case MAY_MATCH:
                goto end;
        }
    }
end:
    free_node_vec(&vars);
    free_node_vec(&vals);
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

node_t simplify_node(mod_t mod, node_t node) {
    switch (node->tag) {
        case NODE_INS:
            return simplify_ins(mod, node);
        case NODE_EXT:
            return simplify_ext(mod, node);
        case NODE_RECORD:
            return simplify_record(node);
        case NODE_LET:
            return simplify_let(mod, node);
        case NODE_ARROW:
            // If the codomain of an arrow does not depend on its variable, mark the variable as unbound
            if (!is_unbound_var(node->arrow.var) && !contains_var(node->arrow.codom->free_vars, node->arrow.var))
                return new_arrow(mod, new_unbound_var(mod, node->arrow.var->type, &node->arrow.var->loc), node->arrow.codom, &node->loc);
            return node;
        case NODE_ABS:
            // If the body of an abstraction does not depend on its variable, mark the variable as unbound
            if (!is_unbound_var(node->abs.var) && !contains_var(node->abs.body->free_vars, node->abs.var))
                return new_abs(mod, new_unbound_var(mod, node->abs.var->type, &node->abs.var->loc), node->abs.body, &node->loc);
            // Eta-expansion: \x . f x => f
            if (node->abs.body->tag == NODE_APP &&
                node->abs.body->app.left->type == node->type &&
                node->abs.body->app.right == node->abs.var)
                return node->abs.body->app.left;
            return node;
        case NODE_BOT:
        case NODE_TOP:
            if (node->type->tag == NODE_PROD) {
                node_t* args = new_buf(node_t, node->type->prod.arg_count);
                for (size_t i = 0, n = node->type->prod.arg_count; i < n; ++i) {
                    args[i] = node->tag == NODE_TOP
                        ? new_top(mod, node->type->prod.args[i], &node->loc)
                        : new_bot(mod, node->type->prod.args[i], &node->loc);
                }
                node_t res = new_record(mod, args, node->type->prod.labels, node->type->prod.arg_count, &node->loc);
                free_buf(args);
                return res;
            }
            return node;
        case NODE_LETREC:
            return simplify_letrec(mod, node);
        case NODE_MATCH:
            return simplify_match(mod, node);
        default:
            return node;
    }
}
