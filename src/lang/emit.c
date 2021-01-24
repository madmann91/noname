#include "lang/ast.h"
#include "utils/buf.h"
#include "utils/format.h"
#include "ir/node.h"

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

static node_t infer_exp(struct emitter*, struct ast*);
static node_t check_exp(struct emitter*, struct ast*, node_t);
static node_t emit_exp(struct emitter*, struct ast*);

// Helpers -------------------------------------------------------------------------

static inline node_t cannot_infer(struct emitter* emitter, struct ast* ast, const char* msg) {
    log_error(emitter->log, &ast->loc, "cannot infer type for %0:s", FORMAT_ARGS({ .s = msg }));
    return new_top(emitter->mod, new_star(emitter->mod), NULL);
}

static inline label_t get_numbered_label(mod_t mod, const char* prefix, size_t i, const struct loc* loc) {
    size_t len = strlen(prefix);
    char* buf = new_buf(char, LABEL_BUF_SIZE + len + 1);
    snprintf(buf, LABEL_BUF_SIZE + len + 1, "%s_%zu", prefix, i);
    label_t label = new_label(mod, buf, loc);
    free_buf(buf);
    return label;
}

static inline label_t get_fresh_label(struct emitter* emitter, const char* prefix, const struct loc* loc) {
    return get_numbered_label(emitter->mod, prefix, emitter->var_index++, loc);
}

static inline const label_t* get_tuple_labels(struct emitter* emitter, size_t index) {
    if (emitter->tuple_labels.size <= index) {
        for (size_t i = emitter->tuple_labels.size; i <= index; ++i)
            push_to_label_vec(&emitter->tuple_labels, get_numbered_label(emitter->mod, "", i, NULL));
    }
    return emitter->tuple_labels.elems;
}

static inline node_t get_fresh_var(struct emitter* emitter, node_t type, const char* prefix, const struct loc* loc) {
    return new_var(emitter->mod, type, get_fresh_label(emitter, prefix, loc), loc);
}

static inline node_t get_var_for_param(struct emitter* emitter, node_t type, struct ast* param) {
    if (param->tag == AST_ANNOT)
        param = param->annot.ast;
    if (param->tag == AST_IDENT)
        return new_var(emitter->mod, type, new_label(emitter->mod, param->ident.name, &param->loc), &param->loc);
    return get_fresh_var(emitter, type, "param", &param->loc);
}

// Inference and checking ----------------------------------------------------------

static inline node_t infer_pat(struct emitter* emitter, struct ast* pat, struct ast* exp) {
    // TODO: Be a bit more clever for tuples/records
    return check_exp(emitter, pat, infer_exp(emitter, exp));
}

static node_t infer_exp(struct emitter* emitter, struct ast* ast) {
    if (ast->type)
        return ast->type;
    switch (ast->tag) {
        case AST_NAT:
            return new_star(emitter->mod);
        case AST_INT:
        case AST_FLOAT:
            return ast->type = new_arrow(
                emitter->mod,
                new_unbound_var(emitter->mod, new_star(emitter->mod), NULL),
                new_star(emitter->mod), NULL);
        case AST_LET:
        case AST_LETREC:
            for (struct ast* name = ast->let.names, *val = ast->let.vals; name; name = name->next, val = val->next)
                infer_pat(emitter, name, val);
            return ast->type = infer_exp(emitter, ast->let.body); 
        case AST_MATCH: {
            for (struct ast* pat = ast->match.pats; pat; pat = pat->next)
                infer_pat(emitter, pat, ast->match.arg);
            node_t val_type = infer_exp(emitter, ast->match.vals);
            for (struct ast* val = ast->match.vals->next; val; val = val->next)
                check_exp(emitter, val, val_type);
            return ast->type = val_type;
        }
        case AST_RECORD: {
            size_t arg_count = get_ast_list_length(ast->record.args);
            node_t* args = new_buf(node_t, arg_count);
            label_t* fields = new_buf(label_t, arg_count);
            size_t i = 0;
            for (struct ast* arg = ast->record.args; arg; arg = arg->next, i++)
                args[i] = infer_exp(emitter, arg);
            i = 0;
            for (struct ast* field = ast->record.fields; field; field = field->next, i++)
                fields[i] = new_label(emitter->mod, field->ident.name, &field->loc);
            node_t prod = new_prod(emitter->mod, args, fields, arg_count, &ast->loc);
            free_buf(args);
            free_buf(fields);
            return ast->type = prod;
        }
        case AST_ARROW: {
            infer_exp(emitter, ast->arrow.dom);
            return ast->type = infer_exp(emitter, ast->arrow.codom);
        }
        case AST_ABS: {
            node_t param_type = infer_exp(emitter, ast->abs.param);
            node_t body_type = infer_exp(emitter, ast->abs.body);
            node_t var = get_var_for_param(emitter, param_type, ast->abs.param);
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
                node_t max_bitwidth = new_lit(
                    emitter->mod, new_nat(emitter->mod),
                    &(struct lit) { .tag = LIT_INT, .int_val = 64 }, &ast->loc);
                return ast->type = new_app(emitter->mod, new_float(emitter->mod), max_bitwidth, &ast->loc);
            }
        case AST_APP: {
            node_t left_type  = infer_exp(emitter, ast->app.left);
            node_t right_type = infer_exp(emitter, ast->app.right);
            if (left_type->tag != NODE_ARROW)
                return cannot_infer(emitter, ast, "application");
            return ast->type = replace_var(left_type->arrow.codom, left_type->arrow.var, right_type);
        }
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

static node_t check_exp(struct emitter* emitter, struct ast* ast, node_t expected_type) {
    assert(!ast->type && "cannot check AST nodes more than once");
    assert(expected_type);
    switch (ast->tag) {
        case AST_IDENT:
            return ast->type = expected_type;
        default: {
            node_t type = infer_exp(emitter, ast);
            if (type != expected_type) {
                log_error(emitter->log, &ast->loc,
                    "expected type '%0:e', but got '%1:e'",
                    FORMAT_ARGS({ .n = expected_type }, { .n = type }));
                return new_err(emitter->mod, expected_type, &ast->loc);
            }
            return ast->type = type;
        }
    }
}

// IR emission ---------------------------------------------------------------------

static inline node_t emit_ident(struct emitter* emitter, struct ast* ast) {
    return new_var(emitter->mod, ast->type, get_fresh_label(emitter, ast->ident.name, &ast->loc), &ast->loc);
}

static inline node_t emit_lit(struct emitter* emitter, struct ast* ast) {
    return new_lit(emitter->mod, ast->type, &ast->lit, &ast->loc);
}

static node_t emit_record(struct emitter* emitter, struct ast* ast, node_t (*emit_arg)(struct emitter*, struct ast*)) {
    size_t arg_count = get_ast_list_length(ast->record.args);
    node_t* args = new_buf(node_t, arg_count);
    label_t* fields = new_buf(label_t, arg_count);
    size_t i = 0;
    for (struct ast* arg = ast->record.args; arg; arg = arg->next, i++)
        args[i] = emit_arg(emitter, arg);
    i = 0;
    for (struct ast* field = ast->record.fields; field; field = field->next, i++)
        fields[i] = new_label(emitter->mod, field->ident.name, &field->loc);
    node_t record = new_record(emitter->mod, args, fields, arg_count, &ast->loc);
    free_buf(args);
    free_buf(fields);
    return record;
}

static node_t emit_pat(struct emitter* emitter, struct ast* ast) {
    assert(ast->type);
    assert(!ast->node);
    switch (ast->tag) {
        case AST_ANNOT:
            return ast->node = emit_pat(emitter, ast->annot.ast);
        case AST_IDENT:
            return ast->node = emit_ident(emitter, ast);
        case AST_LIT:
            return ast->node = emit_lit(emitter, ast);
        case AST_RECORD:
            return ast->node = emit_record(emitter, ast, emit_pat);
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

static node_t emit_exp(struct emitter* emitter, struct ast* ast) {
    if (ast->node)
        return ast->node;
    infer_exp(emitter, ast);
    switch (ast->tag) {
        case AST_NAT:   return ast->node = new_nat(emitter->mod);
        case AST_INT:   return ast->node = new_int(emitter->mod);
        case AST_FLOAT: return ast->node = new_float(emitter->mod);
        case AST_LET:
        case AST_LETREC: {
            size_t var_count = get_ast_list_length(ast->let.names);
            node_t* vars = new_buf(node_t, var_count);
            node_t* vals = new_buf(node_t, var_count);
            size_t i = 0;
            for (struct ast* name = ast->let.names; name; name = name->next, i++)
                vars[i] = emit_pat(emitter, name);
            i = 0;
            for (struct ast* val = ast->let.vals; val; val = val->next, i++)
                vals[i] = emit_exp(emitter, val);
            node_t body = emit_exp(emitter, ast->let.body);
            node_t let = ast->tag == AST_LET
                ? new_let(emitter->mod, vars, vals, var_count, body, &ast->loc)
                : new_letrec(emitter->mod, vars, vals, var_count, body, &ast->loc);
            free_buf(vars);
            free_buf(vals);
            return ast->node = let;
        }
        case AST_MATCH: {
            size_t pat_count = get_ast_list_length(ast->match.pats);
            node_t* pats = new_buf(node_t, pat_count);
            node_t* vals = new_buf(node_t, pat_count);
            size_t i = 0;
            for (struct ast* pat = ast->match.pats, *val = ast->match.vals; pat; pat = pat->next, val = val->next, i++) {
                pats[i] = emit_pat(emitter, pat);
                vals[i] = emit_exp(emitter, val);
            }
            node_t arg = emit_exp(emitter, ast->match.arg);
            node_t match = new_match(emitter->mod, pats, vals, pat_count, arg, &ast->loc);
            free_buf(pats);
            free_buf(vals);
            return ast->node = match;
        }
        case AST_RECORD:
            return ast->node = emit_record(emitter, ast, emit_exp);
        case AST_ARROW: {
            node_t dom = emit_exp(emitter, ast->arrow.dom);
            node_t codom = emit_exp(emitter, ast->arrow.codom);
            node_t var = new_unbound_var(emitter->mod, dom, &ast->arrow.dom->loc);
            return ast->node = new_arrow(emitter->mod, var, codom, &ast->loc);
        }
        case AST_ABS: {
            node_t var = infer_exp(emitter, ast)->arrow.var;
            node_t pat = emit_pat(emitter, ast->abs.param);
            node_t body = emit_exp(emitter, ast->abs.body);
            if (is_unbound_var(var))
                var = get_var_for_param(emitter, var->type, ast->abs.param);
            body = new_match(emitter->mod, &pat, &body, 1, var, &ast->abs.param->loc);
            return ast->node = new_abs(emitter->mod, var, body, &ast->loc);
        }
        case AST_ANNOT:
            return ast->node = emit_exp(emitter, ast->annot.ast);
        case AST_IDENT:
            assert(ast->ident.to);
            return ast->node = emit_exp(emitter, ast->ident.to);
        case AST_LIT:
            return ast->node = emit_lit(emitter, ast);
        case AST_APP: {
            node_t left = emit_exp(emitter, ast->app.left);
            node_t right = emit_exp(emitter, ast->app.right);
            return ast->node = new_app(emitter->mod, left, right, &ast->loc);
        }
        default:
            assert(false && "invalid AST node type");
            return NULL;
    }
}

node_t emit_node(struct ast* ast, mod_t mod, struct log* log) {
    struct emitter emitter = {
        .log = log,
        .mod = mod,
        .tuple_labels = new_label_vec()
    };
    node_t node = emit_exp(&emitter, ast);
    free_label_vec(&emitter.tuple_labels);
    return node;
}
