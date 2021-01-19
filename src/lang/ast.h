#ifndef LANG_AST_H
#define LANG_AST_H

#include "ir/exp.h"
#include "utils/arena.h"

struct ident {
    const char* name;
    struct ast* to;
};

struct ast {
    enum {
        AST_LIT,
        AST_IDENT,
        AST_ANNOT,
        AST_INT,
        AST_FLOAT,
        AST_APP,
        AST_ABS,
        AST_LET,
        AST_LETREC,
        AST_TUP,
        AST_PROD,
        AST_ARRAY,
        AST_ERR
    } tag;
    struct loc loc;
    struct ast* next;
    exp_t type;
    exp_t exp;
    union {
        struct lit lit;
        struct {
            struct ast* ast;
            struct ast* type;
        } annot;
        struct {
            struct ast* left;
            struct ast* right;
        } app;
        struct {
            struct ast* names;
            struct ast* values;
            struct ast* body;
        } let, letrec;
        struct {
            struct ast* param;
            struct ast* body;
        } abs;
        struct {
            struct ast* args;
        } tup, prod;
        struct {
            struct ast* elem;
            struct ast* dim;
        } array;
        struct ident ident;
    };
};

struct ast* parse(
    struct arena** arena,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

void bind(struct ast*, struct log*);
exp_t emit(struct ast*, mod_t, struct log*);

#endif
