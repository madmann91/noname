#ifndef EXP_H
#define EXP_H

#include <stddef.h>

struct exp;

struct pat {
    enum {
        PAT_BVAR,
        PAT_FVAR,
        PAT_WILD,
        PAT_INJ,
        PAT_TUP
    } tag;
    const struct exp* type;
    union {
        struct {
            size_t index;
        } bvar;
        struct {
            const char* name;
        } fvar;
        struct {
            const struct pat* arg;
            size_t index;
        } inj;
        struct {
            const struct pat* args;
            size_t arg_count;
        } tup;
    };
};

struct exp {
    enum {
        EXP_BVAR,
        EXP_FVAR,
        EXP_UNI,
        EXP_STAR,
        EXP_INT,
        EXP_REAL,
        EXP_SUM,
        EXP_PROD,
        EXP_PI,
        EXP_INJ,
        EXP_TUP,
        EXP_ABS,
        EXP_APP,
        EXP_LET
    } tag;
    const struct exp* type;
    union {
        struct {
            size_t index;
            size_t sub_index;
        } bvar;
        struct {
            const char* name;
        } fvar;
        struct {
            size_t bitwidth;
        } int_, real;
        struct {
            const struct exp* args;
            size_t arg_count;
        } tup, prod, sum;
        struct {
            const struct exp* dom;
            const struct exp* codom;
        } pi;
        struct {
            const struct exp* arg;
            size_t index;
        } inj;
        struct {
            const struct pat* pat;
            const struct exp* body;
        } abs;
        struct {
            const struct exp* left;
            const struct exp* right;
        } app;
        struct {
            const struct exp** binds;
            const struct exp* body;
            size_t bind_count;
        } let, letrec;
    };
};

#endif
