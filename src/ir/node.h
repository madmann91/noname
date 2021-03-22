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

struct arena;

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
        NODE_ERR,
        NODE_UNDEF,
        NODE_UNI,
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
        NODE_FUN,
        NODE_APP,
        NODE_LET,
        NODE_LETREC,
        NODE_MATCH
    } tag;
    struct loc loc;
    size_t depth;
    vars_t free_vars;
    vars_t bound_vars;
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
            node_t record;
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
        } fun;
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

node_t make_uni(mod_t);
node_t make_star(mod_t);
node_t make_nat(mod_t);
node_t make_int(mod_t);
node_t make_float(mod_t);
node_t make_arrow(mod_t, node_t, node_t, const struct loc*);
node_t make_fun(mod_t, node_t, node_t, const struct loc*);
node_t make_non_binding_arrow(mod_t, node_t, node_t, const struct loc*);
node_t make_non_binding_fun(mod_t, node_t, node_t, const struct loc*);
node_t make_untyped_err(mod_t, const struct loc*);
node_t make_unbound_var(mod_t, node_t, const struct loc*);
node_t make_undef(mod_t);
node_t make_nat_lit(mod_t, uintmax_t, const struct loc*);
node_t make_int_app(mod_t, node_t, const struct loc*);
node_t make_float_app(mod_t, node_t, const struct loc*);
vars_t make_empty_vars(mod_t);

mod_t get_mod(node_t);

bool is_pat(node_t);
bool is_trivial_pat(node_t);
bool is_unbound_var(node_t);
bool is_int_app(node_t);
bool is_float_app(node_t);
bool has_err(node_t);
bool has_undef(node_t);
static inline bool is_int_or_float_app(node_t node) { return is_int_app(node) || is_float_app(node); }

vars_t make_vars(mod_t, const node_t*, size_t);
vars_t union_vars(mod_t, vars_t, vars_t);
vars_t intr_vars(mod_t, vars_t, vars_t);
vars_t diff_vars(mod_t, vars_t, vars_t);
bool contains_vars(vars_t, vars_t);
bool contains_var(vars_t, node_t);

label_t make_label(mod_t, const char*, const struct loc*);
size_t find_label(const label_t*, size_t, label_t);
size_t find_label_in_node(node_t, label_t);

node_t rebuild_node(node_t);
node_t import_node(mod_t, node_t);
node_t replace_var(node_t, node_t, node_t);
node_t replace_vars(node_t, const node_t*, const node_t*, size_t);
node_t reduce_node(node_t);

node_t parse_node(mod_t, struct arena**, struct log*, const char*, const char*, size_t);
node_t check_node(mod_t, struct log*, node_t);

void print_node(struct format_out*, node_t);
void dump_node(node_t);
void dump_vars(vars_t);

#endif
