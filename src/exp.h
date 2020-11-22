#ifndef EXP_H
#define EXP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "log.h"

/*
 * Expressions are hash-consed. Variables are represented using indices.
 * By default, no convention is enforced, but Axelsson-Claessen-style indices
 * (based on the depth of the enclosed expression) can be used to obtain
 * alpha-equivalence.
 */

typedef struct mod* mod_t;
typedef const struct exp* exp_t;
typedef const struct fvs* fvs_t;

union lit {
    uintmax_t int_val;
    double    real_val;
};

struct fvs {
    exp_t* vars;
    size_t count;
};

struct exp {
    enum {
        EXP_VAR,
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
    size_t depth;
    fvs_t fvs;
    exp_t type;
    union {
        struct {
            struct mod* mod;
        } uni;
        struct {
            size_t index;
        } var;
        struct {
            exp_t bitwidth;
        } int_, real;
        union lit lit;
        struct {
            exp_t* args;
            size_t arg_count;
        } tup, prod, sum;
        struct {
            exp_t var;
            exp_t dom;
            exp_t codom;
        } pi;
        struct {
            exp_t arg;
            size_t index;
        } inj;
        struct {
            exp_t var;
            exp_t body;
        } abs;
        struct {
            exp_t left;
            exp_t right;
        } app;
        struct {
            exp_t* vars;
            exp_t* vals;
            size_t var_count;
            exp_t body;
        } let, letrec;
        struct {
            exp_t* pats;
            exp_t* vals;
            size_t pat_count;
            exp_t arg;
        } match;
    };
};

mod_t new_mod(void);
void free_mod(mod_t);

mod_t get_mod(exp_t);
bool is_pat(exp_t);

fvs_t new_fvs(mod_t, exp_t*, size_t);
fvs_t new_fv(mod_t, exp_t);
fvs_t union_fvs(mod_t, fvs_t, fvs_t);
fvs_t diff_fvs(mod_t, fvs_t, fvs_t);
bool contains_fvs(fvs_t, fvs_t);
bool contains_fv(fvs_t, exp_t);

exp_t new_var(mod_t, exp_t, size_t, const struct loc*);
exp_t new_uni(mod_t);
exp_t new_star(mod_t);
exp_t new_nat(mod_t);
exp_t new_wild(mod_t, exp_t, const struct loc*);
exp_t new_top(mod_t, exp_t, const struct loc*);
exp_t new_bot(mod_t, exp_t, const struct loc*);
exp_t new_int(mod_t, exp_t, const struct loc*);
exp_t new_real(mod_t, exp_t, const struct loc*);
exp_t new_lit(mod_t, exp_t, const union lit*, const struct loc*);
exp_t new_sum(mod_t, exp_t*, size_t, const struct loc*);
exp_t new_prod(mod_t, exp_t*, size_t, const struct loc*);
exp_t new_pi(mod_t, exp_t, exp_t, exp_t, const struct loc*);
exp_t new_inj(mod_t, exp_t, size_t, exp_t, const struct loc*);
exp_t new_tup(mod_t, exp_t*, size_t, const struct loc*);
exp_t new_abs(mod_t, exp_t, exp_t, const struct loc*);
exp_t new_app(mod_t, exp_t, exp_t, const struct loc*);
exp_t new_let(mod_t, exp_t*, exp_t*, size_t, exp_t, const struct loc*);
exp_t new_letrec(mod_t, exp_t*, exp_t*, size_t, exp_t, const struct loc*);
exp_t new_match(mod_t, exp_t*, exp_t*, size_t, exp_t, const struct loc*);

exp_t rebuild_exp(exp_t);
exp_t import_exp(mod_t, exp_t);
exp_t replace_exp(exp_t, exp_t, exp_t);
exp_t reduce_exp(exp_t);

#endif
