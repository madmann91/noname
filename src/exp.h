#ifndef EXP_H
#define EXP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "log.h"

/*
 * Expressions are hash-consed. Inside expressions,
 * free variables (`FVAR`s) are separated from bound
 * variables (`BVAR`s). Bound variables use De Bruijn
 * indices, and free variables use unique indices.
 * Since `LET`-expressions or `MATCH`-expressions may
 * introduce more than one variable at a time, bound
 * variables have a sub-index that selects which
 * variable is referred to.
 */

typedef struct mod* mod_t;
typedef const struct exp* exp_t;

union lit {
    uintmax_t int_val;
    double    real_val;
};

struct exp {
    enum {
        EXP_BVAR,
        EXP_FVAR,
        EXP_UNI,
        EXP_STAR,
        EXP_NAT,
        EXP_WILD,
        EXP_TOP,
        EXP_BOT,
        EXP_INT,
        EXP_REAL,
        EXP_LIT,
        EXP_SUM,
        EXP_PROD,
        EXP_PI,
        EXP_INJ,
        EXP_TUP,
        EXP_ABS,
        EXP_APP,
        EXP_LET,
        EXP_LETREC,
        EXP_MATCH
    } tag;
    struct loc loc;
    exp_t type;
    //fvs_t fvs;
    union {
        struct {
            struct mod* mod;
        } uni;
        struct {
            size_t index;
            size_t sub_index;
        } bvar;
        struct {
            size_t index;
        } fvar;
        struct {
            exp_t sub_pat;
        } wild;
        struct {
            exp_t bitwidth;
        } int_, real;
        union lit lit;
        struct {
            exp_t* args;
            size_t arg_count;
        } tup, prod, sum;
        struct {
            exp_t dom, codom;
        } pi;
        struct {
            exp_t arg;
            size_t index;
        } inj;
        struct {
            exp_t body;
        } abs;
        struct {
            exp_t left;
            exp_t right;
        } app;
        struct {
            exp_t* binds;
            exp_t* types;       // NULL for `letrec`
            size_t bind_count;
            exp_t body;
        } let, letrec;
        struct {
            exp_t arg;
            exp_t* pats;
            exp_t* exps;
            size_t pat_count;
        } match;
    };
};

mod_t new_mod(void);
void free_mod(mod_t);

mod_t get_mod_from_exp(exp_t);

exp_t rebuild_exp(exp_t);
exp_t import_exp(mod_t, exp_t);

exp_t open_exp(size_t, exp_t, exp_t*, size_t);
exp_t close_exp(size_t, exp_t, exp_t*, size_t);
exp_t shift_exp(size_t, exp_t, size_t, bool);

#endif
