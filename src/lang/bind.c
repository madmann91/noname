#include "lang/ast.h"
#include "utils/format.h"

VEC(ident_vec, struct ident)
VEC(scope_vec, size_t)

struct binder {
    struct {
        struct ident_vec idents;
        struct scope_vec scopes;
    } env;
    struct log* log;
};

// Environment manipulation --------------------------------------------------------

static inline void push_scope(struct binder* binder) {
    push_to_scope_vec(&binder->env.scopes, binder->env.idents.size);
}

static inline void pop_scope(struct binder* binder) {
    size_t last = pop_from_scope_vec(&binder->env.scopes);
    resize_ident_vec(&binder->env.idents, last);
}

static void insert_ident(struct binder* binder, const char* name, struct ast* to) {
    // Identifiers that begin with an underscore are not bound
    if (name[0] == '_')
        return;
    size_t last = binder->env.scopes.elems[binder->env.scopes.size - 1];
    for (size_t i = last, n = binder->env.idents.size; i < n; ++i) {
        struct ident* ident = &binder->env.idents.elems[i];
        if (!strcmp(ident->name, name)) {
            log_error(binder->log, &to->loc, "redeclaration of identifier '%0:s'", FORMAT_ARGS({ .s = name }));
            log_note(binder->log, &ident->to->loc, "previously declared here", NULL);
            return;
        }
    }
    for (size_t i = 0; i < last; ++i) {
        struct ident* ident = &binder->env.idents.elems[i];
        if (!strcmp(ident->name, name)) {
            log_warn(binder->log, &to->loc, "identifier '%0:s' shadows previous declaration", FORMAT_ARGS({ .s = name }));
            log_note(binder->log, &ident->to->loc, "previously declared here", NULL);
            break;
        }
    }
    push_to_ident_vec(&binder->env.idents, (struct ident) { .name = name, .to = to });
}

static struct ast* find_ident(struct binder* binder, struct loc* loc, const char* name) {
    for (size_t i = binder->env.idents.size; i > 0; --i) {
        struct ident* ident = &binder->env.idents.elems[i - 1];
        if (!strcmp(ident->name, name))
            return ident->to;
    }
    log_error(binder->log, loc, "unknown identifier '%0:s'", FORMAT_ARGS({ .s = name }));
    return NULL;
}

// Binding functions ---------------------------------------------------------------

static void bind_exp(struct binder*, struct ast*);

static void bind_pat(struct binder* binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_IDENT:
            insert_ident(binder, ast->ident.name, ast);
            break;
        case AST_ANNOT:
            bind_exp(binder, ast->annot.type);
            bind_pat(binder, ast->annot.ast);
            break;
        case AST_RECORD:
            for (struct ast* arg = ast->record.args; arg; arg = arg->next)
                bind_pat(binder, arg);
            break;
        default:
            assert(false && "invalid AST pattern");
            // fallthrough
        case AST_LIT:
            break;
    }
}

static void bind_exp(struct binder* binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_LET:
        case AST_LETREC:
            push_scope(binder);
            if (ast->tag == AST_LETREC) {
                for (struct ast* var = ast->let.vars; var; var = var->next)
                    bind_pat(binder, var);
            }
            for (struct ast* val = ast->let.vals; val; val = val->next)
                bind_exp(binder, val);
            push_scope(binder);
            if (ast->tag == AST_LET) {
                for (struct ast* var = ast->let.vars; var; var = var->next)
                    bind_pat(binder, var);
            }
            bind_exp(binder, ast->let.body);
            pop_scope(binder);
            pop_scope(binder);
            break;
        case AST_MATCH:
            bind_exp(binder, ast->match.arg);
            for (struct ast* pat = ast->match.pats, *val = ast->match.vals; pat; pat = pat->next, val = val->next) {
                push_scope(binder);
                bind_pat(binder, pat);
                bind_exp(binder, val);
                pop_scope(binder);
            }
            break;
        case AST_ARROW:
            bind_exp(binder, ast->arrow.dom);
            bind_exp(binder, ast->arrow.codom);
            break;
        case AST_ABS:
            push_scope(binder);
            bind_pat(binder, ast->abs.param);
            bind_exp(binder, ast->abs.body);
            pop_scope(binder);
            break;
        case AST_INS:
            bind_exp(binder, ast->ins.val);
            bind_exp(binder, ast->ins.record);
            break;
        case AST_EXT:
            bind_exp(binder, ast->ext.val);
            break;
        case AST_PROD:
        case AST_RECORD:
            for (struct ast* arg = ast->record.args; arg; arg = arg->next)
                bind_exp(binder, arg);
            break;
        case AST_IDENT:
            ast->ident.to = find_ident(binder, &ast->loc, ast->ident.name);
            break;
        case AST_APP:
            bind_exp(binder, ast->app.left);
            bind_exp(binder, ast->app.right);
            break;
        default:
            assert(false && "invalid AST node tag");
            // fallthrough
        case AST_LIT:
        case AST_NAT:
        case AST_INT:
        case AST_FLOAT:
            break;
    }
}

void bind_ast(struct ast* ast, struct log* log) {
    struct binder binder = {
        .env = {
            .idents = new_ident_vec(),
            .scopes = new_scope_vec()
        },
        .log = log
    };
    push_scope(&binder);
    bind_exp(&binder, ast);
    pop_scope(&binder);
    free_ident_vec(&binder.env.idents);
    free_scope_vec(&binder.env.scopes);
}
