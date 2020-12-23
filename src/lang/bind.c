#include "lang/ast.h"
#include "utils/format.h"

struct ident {
    const char* name;
    struct ast* to;
};

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
    size_t last = binder->env.scopes.elems[binder->env.scopes.size - 1];
    for (size_t i = last, n = binder->env.idents.size; i < n; ++i) {
        struct ident* ident = &binder->env.idents.elems[i];
        if (!strcmp(ident->name, name)) {
            log_error(binder->log, &to->loc, "redeclaration of identifier '%0:s'", FMT_ARGS({ .s = name }));
            log_note(binder->log, &ident->to->loc, "previously declared here", NULL);
            return;
        }
    }
    for (size_t i = 0; i < last; ++i) {
        struct ident* ident = &binder->env.idents.elems[i];
        if (!strcmp(ident->name, name)) {
            log_warn(binder->log, &to->loc, "shadowing identifier '%0:s'", FMT_ARGS({ .s = name }));
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
    log_error(binder->log, loc, "unknown identifier '%0:s'", FMT_ARGS({ .s = name }));
    return NULL;
}

// Binding functions ---------------------------------------------------------------

static void bind_head(struct binder* binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_FUN:
            insert_ident(binder, ast->fun.name->ident.str, ast);
            break;
        default:
            break;
    }
}

static void bind(struct binder*, struct ast*);

static void bind_pat(struct binder* binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_IDENT:
            insert_ident(binder, ast->ident.str, ast);
            break;
        case AST_ANNOT:
            bind(binder, ast->annot.type);
            bind_pat(binder, ast->annot.ast);
            break;
        case AST_TUP: {
            for (struct ast* arg = ast->tup.args; arg; arg = arg->next)
                bind_pat(binder, arg);
            break;
        }
        default:
            assert(false && "invalid AST pattern");
            break;
    }
}

static void bind(struct binder* binder, struct ast* ast) {
    switch (ast->tag) {
        case AST_MOD: {
            for (struct ast* decl = ast->mod.decls; decl; decl = decl->next)
                bind_head(binder, decl);
            for (struct ast* decl = ast->mod.decls; decl; decl = decl->next)
                bind(binder, decl);
            break;
        }
        case AST_FUN:
            if (ast->fun.ret_type)
                bind(binder, ast->fun.ret_type);
            push_scope(binder);
            bind_pat(binder, ast->fun.param);
            bind(binder, ast->fun.body);
            pop_scope(binder);
            break;
        case AST_IDENT:
            ast->ident.to = find_ident(binder, &ast->loc, ast->ident.str);
            break;
        case AST_APP:
            bind(binder, ast->app.left);
            bind(binder, ast->app.right);
            break;
        default:
            assert(false && "invalid AST node tag");
            // fallthrough
        case AST_LIT:
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
    bind(&binder, ast);
    pop_scope(&binder);
    free_ident_vec(&binder.env.idents);
    free_scope_vec(&binder.env.scopes);
}
