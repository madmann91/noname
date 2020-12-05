#ifndef EXP_H
#define EXP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "log.h"
#include "htable.h"

/*
 * Expressions are hash-consed. Variables are represented using indices.
 * By default, no convention is enforced, but Axelsson-Claessen-style indices
 * (based on the depth of the enclosed expression) can be used to obtain
 * alpha-equivalence.
 */

typedef struct mod* mod_t;
typedef const struct exp* exp_t;
typedef const struct vars* vars_t;

union lit {
    uintmax_t int_val;
    double    real_val;
};

struct vars {
    const exp_t* vars;
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
        EXP_INS,
        EXP_EXT,
        EXP_ABS,
        EXP_APP,
        EXP_LET,
        EXP_LETREC,
        EXP_MATCH
    } tag;
    struct loc loc;
    size_t depth;
    vars_t free_vars;
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
            const exp_t* args;
            size_t arg_count;
        } tup, prod, sum;
        struct {
            exp_t val;
            exp_t index;
        } ext;
        struct {
            exp_t val;
            exp_t index;
            exp_t elem;
        } ins;
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
            const exp_t* vars;
            const exp_t* vals;
            size_t var_count;
            exp_t body;
        } let, letrec;
        struct {
            const exp_t* pats;
            const exp_t* vals;
            size_t pat_count;
            exp_t arg;
        } match;
    };
};

mod_t new_mod(struct log*);
void free_mod(mod_t);

mod_t get_mod(exp_t);

bool is_pat(exp_t);
bool is_trivial_pat(exp_t);
vars_t collect_bound_vars(exp_t);

vars_t new_vars(mod_t, const exp_t*, size_t);
vars_t union_vars(mod_t, vars_t, vars_t);
vars_t intr_vars(mod_t, vars_t, vars_t);
vars_t diff_vars(mod_t, vars_t, vars_t);
bool contains_vars(vars_t, vars_t);
bool contains_var(vars_t, exp_t);

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
exp_t new_sum(mod_t, const exp_t*, size_t, const struct loc*);
exp_t new_prod(mod_t, const exp_t*, size_t, const struct loc*);
exp_t new_pi(mod_t, exp_t, exp_t, exp_t, const struct loc*);
exp_t new_inj(mod_t, exp_t, size_t, exp_t, const struct loc*);
exp_t new_tup(mod_t, const exp_t*, size_t, const struct loc*);
exp_t new_ins(mod_t, exp_t, exp_t, exp_t, const struct loc*);
exp_t new_ext(mod_t, exp_t, exp_t, const struct loc*);
exp_t new_abs(mod_t, exp_t, exp_t, const struct loc*);
exp_t new_app(mod_t, exp_t, exp_t, const struct loc*);
exp_t new_let(mod_t, const exp_t*, const exp_t*, size_t, exp_t, const struct loc*);
exp_t new_letrec(mod_t, const exp_t*, const exp_t*, size_t, exp_t, const struct loc*);
exp_t new_match(mod_t, const exp_t*, const exp_t*, size_t, exp_t, const struct loc*);

exp_t rebuild_exp(exp_t);
exp_t import_exp(mod_t, exp_t);
exp_t replace_exp(exp_t, exp_t, exp_t);
exp_t replace_exps(exp_t, struct htable*);
exp_t reduce_exp(exp_t);

struct htable new_exp_map(void);
struct htable new_exp_set(void);
exp_t find_in_exp_map(struct htable*, exp_t);
bool insert_in_exp_map(struct htable*, exp_t, exp_t);
bool find_in_exp_set(struct htable*, exp_t);
bool insert_in_exp_set(struct htable*, exp_t);

#endif
