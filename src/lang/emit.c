#include "lang/ast.h"
#include "utils/buf.h"
#include "utils/format.h"
#include "ir/exp.h"

#define LAB_BUF_SIZE 64

struct emitter {
    struct log* log;
    size_t var_index;
    struct lab_vec tup_labs;
    mod_t mod;
};

static inline size_t ast_len(struct ast* ast) {
    size_t len = 0;
    while (ast) ast = ast->next, len++;
    return len;
}

static exp_t infer_internal(struct emitter*, struct ast*, exp_t);
static exp_t emit_internal(struct emitter*, struct ast*);

static inline exp_t infer(struct emitter* emitter, struct ast* ast, exp_t expected_type) {
    return ast->type = ast->type ? ast->type : infer_internal(emitter, ast, expected_type);
}

static inline exp_t emit(struct emitter* emitter, struct ast* ast) {
    return ast->exp = ast->exp ? ast->exp : emit_internal(emitter, ast);
}

static inline exp_t cannot_infer(struct emitter* emitter, struct ast* ast, const char* msg) {
    log_error(emitter->log, &ast->loc, "cannot infer type for %0:s", FMT_ARGS({ .s = msg }));
    return new_top(emitter->mod, new_star(emitter->mod), NULL);
}

static inline const lab_t* tup_labs(struct emitter* emitter, size_t index) {
    if (emitter->tup_labs.size <= index) {
        for (size_t i = emitter->tup_labs.size; i <= index; ++i) {
            char buf[LAB_BUF_SIZE];
            snprintf(buf, LAB_BUF_SIZE, "%zu", i);
            push_to_lab_vec(&emitter->tup_labs, new_lab(emitter->mod, buf, NULL));
        }
    }
    return emitter->tup_labs.elems;
}

static exp_t infer_internal(struct emitter* emitter, struct ast* ast, exp_t expected_type) {
    switch (ast->tag) {
        case AST_FUN: {
            exp_t param_type = NULL, body_type = NULL, var = NULL;
            if (expected_type && expected_type->tag == EXP_ARROW) {
                param_type = expected_type->arrow.var->type;
                body_type = expected_type->arrow.codom;
                var = expected_type->arrow.var;
            }
            if (ast->fun.ret_type)
                body_type = emit(emitter, ast->fun.ret_type);
            param_type = infer(emitter, ast->fun.param, param_type);
            body_type = infer(emitter, ast->fun.body, body_type);
            if (!var)
                var = new_var(emitter->mod, param_type, emitter->var_index++, &ast->loc);
            return new_arrow(emitter->mod, var, body_type, &ast->loc);
        }
        case AST_ANNOT:
            return infer(emitter, ast->annot.ast, emit(emitter, ast->annot.type));
        case AST_IDENT:
            if (ast->ident.to)
                return infer(emitter, ast->ident.to, expected_type);
            return expected_type ? expected_type : cannot_infer(emitter, ast, "identifier");
        case AST_TUP: {
            size_t arg_count = ast_len(ast->tup.args);
            exp_t* args = new_buf(exp_t, arg_count);
            size_t i = 0;
            for (struct ast* arg = ast->tup.args; arg; arg = arg->next, i++) {
                exp_t arg_type = NULL;
                if (expected_type && expected_type->tag == EXP_PROD && i < expected_type->prod.arg_count)
                    arg_type = expected_type->prod.args[i];
                args[i] = infer(emitter, arg, arg_type);
            }
            exp_t type = new_prod(emitter->mod, args, tup_labs(emitter, arg_count), arg_count, &ast->loc);
            free_buf(args);
            return type;
        }
        case AST_LIT: {
            if (expected_type)
                return expected_type;
            if (ast->lit.tag == LIT_INT)
                return new_nat(emitter->mod);
            else if (ast->lit.tag == LIT_FLOAT) {
                exp_t max_bitwidth = new_lit(
                    emitter->mod, new_nat(emitter->mod),
                    &(struct lit) { .tag = LIT_INT, .int_val = 64 }, &ast->loc);
                return new_app(emitter->mod, new_float(emitter->mod), max_bitwidth, &ast->loc);
            }
            assert(false && "invalid literal type");
            return NULL;
        }
        case AST_APP: {
            exp_t left_type = infer(emitter, ast->app.left, NULL);
            exp_t right_type = infer(emitter, ast->app.right, NULL);
            if (left_type->tag != EXP_ARROW)
                return cannot_infer(emitter, ast, "application");
            return replace_exp(left_type->arrow.codom, left_type->arrow.var, right_type);
        }
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

static exp_t emit_internal(struct emitter* emitter, struct ast* ast) {
    switch (ast->tag) {
        case AST_MOD: {
            size_t decl_count = ast_len(ast->mod.decls);
            exp_t* vars = new_buf(exp_t, decl_count);
            exp_t* vals = new_buf(exp_t, decl_count);
            size_t i = 0;
            for (struct ast* decl = ast->mod.decls; decl; decl = decl->next, i++)
                decl->exp = vars[i] = new_var(emitter->mod, infer(emitter, decl, NULL), emitter->var_index++, &decl->loc);
            i = 0;
            for (struct ast* decl = ast->mod.decls; decl; decl = decl->next, i++)
                vals[i] = emit_internal(emitter, decl);
            exp_t body = new_tup(emitter->mod, vars, tup_labs(emitter, decl_count), decl_count, &ast->loc);
            exp_t exp = new_letrec(emitter->mod, vars, vals, decl_count, body, &ast->loc);
            free_buf(vars);
            free_buf(vals);
            return exp;
        }
        case AST_FUN: {
            exp_t var = infer(emitter, ast, NULL)->arrow.var;
            exp_t pat = emit(emitter, ast->fun.param);
            exp_t body = emit(emitter, ast->fun.body);
            if (is_unbound_var(var))
                var = new_var(emitter->mod, var->type, emitter->var_index++, &ast->fun.param->loc);
            body = new_match(emitter->mod, &pat, &body, 1, var, &ast->fun.param->loc);
            return new_abs(emitter->mod, var, body, &ast->loc);
        }
        case AST_ANNOT:
            return emit(emitter, ast->annot.ast);
        case AST_IDENT:
            return ast->ident.to
                ? emit(emitter, ast->ident.to)
                : new_var(emitter->mod, infer(emitter, ast, NULL), emitter->var_index++, &ast->loc);
        case AST_INT:   return new_int(emitter->mod);
        case AST_FLOAT: return new_float(emitter->mod);
        case AST_LIT:
            return new_lit(emitter->mod, infer(emitter, ast, NULL), &ast->lit, &ast->loc);
        case AST_TUP: {
            size_t arg_count = ast_len(ast->tup.args);
            exp_t* args = new_buf(exp_t, arg_count);
            size_t i = 0;
            for (struct ast* arg = ast->tup.args; arg; arg = arg->next, i++)
                args[i] = emit(emitter, arg);
            exp_t exp = new_tup(emitter->mod, args, tup_labs(emitter, arg_count), arg_count, &ast->loc);
            free_buf(args);
            return exp;
        }
        case AST_APP: {
            exp_t left = emit(emitter, ast->app.left);
            exp_t right = emit(emitter, ast->app.right);
            return new_app(emitter->mod, left, right, &ast->loc);
        }
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

exp_t emit_exp(struct ast* ast, mod_t mod, struct log* log) {
    struct emitter emitter = {
        .log = log,
        .mod = mod,
        .tup_labs = new_lab_vec()
    };
    exp_t exp = emit(&emitter, ast);
    free_lab_vec(&emitter.tup_labs);
    return exp;
}
