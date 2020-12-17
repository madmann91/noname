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
        AST_FUN,
        AST_TUP,
        AST_ERR
    } tag;
    struct loc loc;
    struct ast* next;
    exp_t type;
    union {
        struct {
            struct ast* decls;
        } mod;
        struct {
            struct ast* ast;
            struct ast* type;
        } annot;
        struct {
        } int_, float_;
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
        union lit lit;
    };
};

struct ast* parse_ast(
    struct arena** arena,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

void bind_ast(struct ast*, struct log*);
void check_ast(struct ast*, struct log*);

exp_t emit_exp(struct ast*);

#endif
