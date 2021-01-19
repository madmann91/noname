#include "lang/ast.h"
#include "utils/buf.h"
#include "utils/format.h"
#include "ir/exp.h"

#define LABEL_BUF_SIZE 64

struct emitter {
    struct log* log;
    size_t var_index;
    struct label_vec tuple_labels;
    mod_t mod;
};

static inline size_t get_ast_list_length(struct ast* ast) {
    size_t len = 0;
    while (ast) ast = ast->next, len++;
    return len;
}

static exp_t infer_exp(struct emitter*, struct ast*);
static exp_t check_exp(struct emitter*, struct ast*, exp_t);
static exp_t emit_exp(struct emitter*, struct ast*);

// Helpers -------------------------------------------------------------------------

static inline exp_t cannot_infer(struct emitter* emitter, struct ast* ast, const char* msg) {
    log_error(emitter->log, &ast->loc, "cannot infer type for %0:s", FORMAT_ARGS({ .s = msg }));
    return new_top(emitter->mod, new_star(emitter->mod), NULL);
}

static inline label_t get_int_label(mod_t mod, size_t i, struct loc* loc) {
    char buf[LABEL_BUF_SIZE];
    snprintf(buf, LABEL_BUF_SIZE, "%zu", i);
    return new_label(mod, buf, loc);
}

static inline const label_t* get_tuple_labels(struct emitter* emitter, size_t index) {
    if (emitter->tuple_labels.size <= index) {
        for (size_t i = emitter->tuple_labels.size; i <= index; ++i) {
            push_to_label_vec(&emitter->tuple_labels, get_int_label(emitter->mod, i, NULL));
        }
    }
    return emitter->tuple_labels.elems;
}

static inline exp_t get_fresh_var(struct emitter* emitter, exp_t type, struct loc* loc) {
    return new_var(emitter->mod, type, get_int_label(emitter->mod, emitter->var_index++, loc), loc);
}

// Inference and checking ----------------------------------------------------------

static exp_t infer_pat(struct emitter* emitter, struct ast* pat, struct ast* exp) {
    // TODO: Be a bit more clever for tuples/records
    return check_exp(emitter, pat, infer_exp(emitter, exp));
}

static exp_t infer_exp(struct emitter* emitter, struct ast* ast) {
    if (ast->type)
        return ast->type;
    switch (ast->tag) {
        case AST_INT:
        case AST_FLOAT:
            return ast->type = new_arrow(
                emitter->mod,
                new_unbound_var(emitter->mod, new_star(emitter->mod), NULL),
                new_star(emitter->mod), NULL);
        case AST_LET:
        case AST_LETREC:
            for (struct ast* name = ast->let.names, *value = ast->let.values; name; name = name->next, value = value->next)
                infer_pat(emitter, name, value);
            return ast->type = infer_exp(emitter, ast->let.body); 
        case AST_ABS: {
            exp_t param_type = infer_exp(emitter, ast->abs.param);
            exp_t body_type = infer_exp(emitter, ast->abs.body);
            exp_t var = get_fresh_var(emitter, param_type, &ast->abs.param->loc);
            return ast->type = new_arrow(emitter->mod, var, body_type, &ast->loc);
        }
        case AST_ANNOT:
            return ast->type = check_exp(emitter, ast->annot.ast, emit_exp(emitter, ast->annot.type));
        case AST_IDENT:
            return ast->type = ast->ident.to ? infer_exp(emitter, ast->ident.to) : cannot_infer(emitter, ast, "identifier");
        case AST_LIT:
            if (ast->lit.tag == LIT_INT)
                return ast->type = new_nat(emitter->mod);
            else {
                assert(ast->lit.tag == LIT_FLOAT);
                exp_t max_bitwidth = new_lit(
                    emitter->mod, new_nat(emitter->mod),
                    &(struct lit) { .tag = LIT_INT, .int_val = 64 }, &ast->loc);
                return ast->type = new_app(emitter->mod, new_float(emitter->mod), max_bitwidth, &ast->loc);
            }
        case AST_APP: {
            exp_t left_type  = infer_exp(emitter, ast->app.left);
            exp_t right_type = infer_exp(emitter, ast->app.right);
            if (left_type->tag != EXP_ARROW)
                return cannot_infer(emitter, ast, "application");
            return ast->type = replace_exp(left_type->arrow.codom, left_type->arrow.var, right_type);
        }
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

static exp_t check_exp(struct emitter* emitter, struct ast* ast, exp_t expected_type) {
    assert(!ast->type && "cannot check AST nodes more than once");
    assert(expected_type);
    switch (ast->tag) {
        default: {
            exp_t type = infer_exp(emitter, ast);
            if (type != expected_type) {
                log_error(emitter->log, &ast->loc,
                    "expected type '%0:e', but got '%1:e'",
                    FORMAT_ARGS({ .e = expected_type }, { .e = type }));
                return new_err(emitter->mod, expected_type, &ast->loc);
            }
            return ast->type = type;
        }
    }
}

// IR emission ---------------------------------------------------------------------

static exp_t emit_pat(struct emitter* emitter, struct ast* ast) {
    assert(ast->type);
    switch (ast->tag) {
        case AST_ANNOT:
            return emit_pat(emitter, ast->annot.ast);
        case AST_IDENT:
            return new_var(emitter->mod, ast->type, new_label(emitter->mod, ast->ident.name, &ast->loc), &ast->loc);
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

static exp_t emit_exp(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_LET:
        case AST_LETREC: {
            infer_exp(emitter, ast);
            size_t var_count = get_ast_list_length(ast->let.names);
            exp_t* vars = new_buf(exp_t, var_count);
            exp_t* vals = new_buf(exp_t, var_count);
            size_t i = 0;
            for (struct ast* name = ast->let.names; name; name = name->next)
                vars[i++] = emit_pat(emitter, name);
            i = 0;
            for (struct ast* value = ast->let.values; value; value = value->next)
                vals[i++] = emit_exp(emitter, value);
            exp_t body = emit_exp(emitter, ast->let.body);
            exp_t exp = ast->tag == AST_LET
                ? new_let(emitter->mod, vars, vals, var_count, body, &ast->loc)
                : new_letrec(emitter->mod, vars, vals, var_count, body, &ast->loc);
            free_buf(vars);
            free_buf(vals);
            return exp;
        }
        case AST_ABS: {
            exp_t var = infer_exp(emitter, ast)->arrow.var;
            exp_t pat = emit_exp(emitter, ast->abs.param);
            exp_t body = emit_exp(emitter, ast->abs.body);
            if (is_unbound_var(var))
                var = get_fresh_var(emitter, var->type, &ast->abs.param->loc);
            body = new_match(emitter->mod, &pat, &body, 1, var, &ast->abs.param->loc);
            return new_abs(emitter->mod, var, body, &ast->loc);
        }
        case AST_ANNOT:
            return emit_exp(emitter, ast->annot.ast);
        case AST_IDENT:
            assert(ast->ident.to);
            return emit_exp(emitter, ast->ident.to);
        case AST_INT:   return new_int(emitter->mod);
        case AST_FLOAT: return new_float(emitter->mod);
        case AST_LIT:
            return new_lit(emitter->mod, infer_exp(emitter, ast), &ast->lit, &ast->loc);
        case AST_APP: {
            exp_t left = emit_exp(emitter, ast->app.left);
            exp_t right = emit_exp(emitter, ast->app.right);
            return new_app(emitter->mod, left, right, &ast->loc);
        }
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

exp_t emit(struct ast* ast, mod_t mod, struct log* log) {
    struct emitter emitter = {
        .log = log,
        .mod = mod,
        .tuple_labels = new_label_vec()
    };
    exp_t exp = emit_exp(&emitter, ast);
    free_label_vec(&emitter.tuple_labels);
    return exp;
}
