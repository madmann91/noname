#ifndef EXP_H
#define EXP_H

#include <stddef.h>
#include <stdint.h>

typedef struct mod* mod_t;
typedef const struct exp* exp_t;
typedef const struct pat* pat_t;

struct pat {
    enum {
        PAT_BVAR,
        PAT_FVAR,
        PAT_WILD,
        PAT_INJ,
        PAT_TUP
    } tag;
    exp_t type;
    union {
        struct {
            size_t index;
        } bvar;
        struct {
            const char* name;
        } fvar;
        struct {
            pat_t arg;
            size_t index;
        } inj;
        struct {
            pat_t* args;
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
        EXP_MATCH
    } tag;
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
            const char* name;
        } fvar;
        struct {
            exp_t bitwidth;
        } int_, real;
        union lit {
            uintmax_t int_val;
            double    real_val;
        } lit;
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
            size_t bind_count;
            exp_t body;
        } let;
        struct {
            exp_t arg;
            pat_t* pats;
            exp_t* exps;
            size_t pat_count;
        } match;
    };
};

mod_t new_mod(void);
void free_mod(mod_t);

mod_t get_mod_from_exp(exp_t);
mod_t get_mod_from_pat(pat_t);

exp_t rebuild_exp(exp_t);
exp_t import_exp(mod_t, exp_t);

exp_t open_exp(size_t, exp_t, exp_t*, size_t);
exp_t close_exp(size_t, exp_t, exp_t*, size_t);

#endif
