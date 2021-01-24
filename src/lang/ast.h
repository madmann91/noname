#ifndef LANG_AST_H
#define LANG_AST_H

#include "ir/node.h"
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
        AST_NAT,
        AST_INT,
        AST_FLOAT,
        AST_APP,
        AST_ARROW,
        AST_ABS,
        AST_INS,
        AST_EXT,
        AST_LET,
        AST_LETREC,
        AST_MATCH,
        AST_RECORD,
        AST_PROD,
        AST_ARRAY,
        AST_ERR
    } tag;
    struct loc loc;
    struct ast* next;
    node_t type;
    node_t node;
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
            struct ast* vals;
            struct ast* body;
        } let, letrec;
        struct {
            struct ast* dom;
            struct ast* codom;
        } arrow;
        struct {
            struct ast* param;
            struct ast* body;
        } abs;
        struct {
            struct ast* val;
            struct ast* elem;
        } ext;
        struct {
            struct ast* val;
            struct ast* record;
        } ins;
        struct {
            struct ast* fields;
            struct ast* args;
        } record, prod;
        struct {
            struct ast* elem;
            struct ast* dim;
        } array;
        struct {
            struct ast* arg;
            struct ast* pats;
            struct ast* vals;
        } match;
        struct ident ident;
    };
};

struct ast* parse_ast(
    struct arena** arena,
    struct log* log,
    const char* file_name,
    const char* data,
    size_t data_size);

// Binds the identifiers that occur in the AST to their declaration site.
// Must be run before emitting IR from an AST.
void bind_ast(struct ast*, struct log*);

// Emits an IR node from the given AST.
// Assumes that identifiers are correctly bound.
// Note that since the IR is dependently-typed,
// type-checking and emission happen at the same time.
node_t emit_node(struct ast*, mod_t, struct log*);

#endif
