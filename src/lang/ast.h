#ifndef LANG_AST_H
#define LANG_AST_H

#include "ir/exp.h"
#include "utils/arena.h"

struct ast {
    enum {
        AST_MOD,
        AST_LIT,
        AST_IDENT,
        AST_ANNOT,
        AST_INT,
        AST_FLOAT,
        AST_APP,
        AST_FUN,
        AST_TUP,
        AST_ERR
    } tag;
    struct loc loc;
    struct ast* next;
    exp_t type;
    exp_t exp;
    union {
        struct {
            struct ast* decls;
        } mod;
        struct lit lit;
        struct {
            struct ast* ast;
            struct ast* type;
        } annot;
        struct app {
            struct ast* left;
            struct ast* right;
        } app;
        struct {
            struct ast* name;
            struct ast* param;
            struct ast* ret_type;
            struct ast* body;
        } fun;
        struct {
            struct ast* args;
        } tup;
        struct {
            const char* str;
            struct ast* to;
        } ident;
    };
};

struct ast* parse_ast(
    struct arena** arena,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

void bind_ast(struct ast*, struct log*);
exp_t emit_exp(struct ast*, mod_t, struct log*);

#endif
