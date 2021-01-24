#ifndef IR_NODE_H
#define IR_NODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "utils/map.h"
#include "utils/set.h"
#include "utils/vec.h"
#include "utils/log.h"

/*
 * Expressions are hash-consed. Variables are represented using names, under the
 * assumption that there is no shadowing. This assumption is important, as it allows
 * to simplify variable replacement.
 * By default, no convention is enforced, but Axelsson-Claessen-style indices
 * (based on the depth of the enclosed expression) can be used to obtain
 * alpha-equivalence.
 */

typedef struct mod* mod_t;
typedef const struct node* node_t;
typedef const struct label* label_t;
typedef const struct vars* vars_t;

struct lit {
    enum {
        LIT_INT,
        LIT_FLOAT
    } tag;
    union {
        uintmax_t int_val;
        double    float_val;
    };
};

struct vars {
    const node_t* vars;
    size_t count;
};

struct label {
    const char* name;
    struct loc loc;
};

struct node {
    enum {
        NODE_UNI,
        NODE_ERR,
        NODE_VAR,
        NODE_STAR,
        NODE_NAT,
        NODE_INT,
        NODE_FLOAT,
        NODE_TOP,
        NODE_BOT,
        NODE_LIT,
        NODE_SUM,
        NODE_PROD,
        NODE_ARROW,
        NODE_INJ,
        NODE_RECORD,
        NODE_INS,
        NODE_EXT,
        NODE_ABS,
        NODE_APP,
        NODE_LET,
        NODE_LETREC,
        NODE_MATCH
    } tag;
    struct loc loc;
    size_t depth;
    vars_t free_vars;
    vars_t decl_vars;
    node_t type;
    union {
        struct {
            struct mod* mod;
        } uni;
        struct {
            label_t label;
        } var;
        struct {
            node_t bitwidth;
        } int_, float_;
        struct lit lit;
        struct {
            const node_t* args;
            const label_t* labels;
            size_t arg_count;
        } record, prod, sum;
        struct {
            node_t val;
            label_t label;
        } ext;
        struct {
            node_t val;
            label_t label;
            node_t elem;
        } ins;
        struct {
            node_t var;
            node_t codom;
        } arrow;
        struct {
            node_t arg;
            label_t label;
        } inj;
        struct {
            node_t var;
            node_t body;
        } abs;
        struct {
            node_t left;
            node_t right;
        } app;
        struct {
            const node_t* vars;
            const node_t* vals;
            size_t var_count;
            node_t body;
        } let, letrec;
        struct {
            const node_t* pats;
            const node_t* vals;
            size_t pat_count;
            node_t arg;
        } match;
    };
};

MAP(node_map, node_t, node_t)
SET(node_set, node_t)
VEC(node_vec, node_t)
VEC(label_vec, label_t)

mod_t new_mod(void);
void free_mod(mod_t);

mod_t get_mod(node_t);

bool is_pat(node_t);
bool is_trivial_pat(node_t);
bool is_unbound_var(node_t);

vars_t new_vars(mod_t, const node_t*, size_t);
vars_t union_vars(mod_t, vars_t, vars_t);
vars_t intr_vars(mod_t, vars_t, vars_t);
vars_t diff_vars(mod_t, vars_t, vars_t);
bool contains_vars(vars_t, vars_t);
bool contains_var(vars_t, node_t);

label_t new_label(mod_t, const char*, const struct loc*);
size_t find_label(const label_t*, size_t, label_t);
size_t find_label_in_node(node_t, label_t);

node_t get_elem_type(node_t, label_t);

node_t new_uni(mod_t);
node_t new_err(mod_t, node_t, const struct loc*);
node_t new_untyped_err(mod_t, const struct loc*);
node_t new_var(mod_t, node_t, label_t, const struct loc*);
node_t new_unbound_var(mod_t, node_t, const struct loc*);
node_t new_star(mod_t);
node_t new_nat(mod_t);
node_t new_int(mod_t);
node_t new_float(mod_t);
node_t new_top(mod_t, node_t, const struct loc*);
node_t new_bot(mod_t, node_t, const struct loc*);
node_t new_lit(mod_t, node_t, const struct lit*, const struct loc*);
node_t new_sum(mod_t, const node_t*, const label_t*, size_t, const struct loc*);
node_t new_prod(mod_t, const node_t*, const label_t*, size_t, const struct loc*);
node_t new_arrow(mod_t, node_t, node_t, const struct loc*);
node_t new_inj(mod_t, node_t, label_t, node_t, const struct loc*);
node_t new_record(mod_t, const node_t*, const label_t*, size_t, const struct loc*);
node_t new_ins(mod_t, node_t, label_t, node_t, const struct loc*);
node_t new_ext(mod_t, node_t, label_t, const struct loc*);
node_t new_abs(mod_t, node_t, node_t, const struct loc*);
node_t new_app(mod_t, node_t, node_t, const struct loc*);
node_t new_let(mod_t, const node_t*, const node_t*, size_t, node_t, const struct loc*);
node_t new_letrec(mod_t, const node_t*, const node_t*, size_t, node_t, const struct loc*);
node_t new_match(mod_t, const node_t*, const node_t*, size_t, node_t, const struct loc*);

node_t rebuild_node(node_t);
node_t import_node(mod_t, node_t);
node_t replace_var(node_t, node_t, node_t);
node_t replace_vars(node_t, const node_t*, const node_t*, size_t);
node_t reduce_node(node_t);

#endif
