#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/arena.h"
#include "utils/hash.h"
#include "utils/format.h"
#include "utils/vec.h"
#include "utils/buf.h"
#include "utils/sort.h"
#include "ir/node.h"

// Hash consing --------------------------------------------------------------------

static inline bool compare_vars(const void*, const void*);
static inline bool compare_label(const void*, const void*);
static inline bool compare_node(const void*, const void*);
static inline uint32_t hash_vars(const void*);
static inline uint32_t hash_label(const void*);
static inline uint32_t hash_node(const void*);
CUSTOM_MAP(mod_nodes, node_t, node_t, hash_node, compare_node)
CUSTOM_SET(mod_labels, label_t, hash_label, compare_label)
CUSTOM_SET(mod_vars, vars_t, hash_vars, compare_vars)

struct mod {
    arena_t arena;
    struct mod_nodes nodes;
    struct mod_labels labels;
    struct mod_vars vars;
    node_t uni, star, nat, int_, float_, undef;
    vars_t empty_vars;
};

// Free variables ------------------------------------------------------------------

static inline bool compare_vars(const void* ptr1, const void* ptr2) {
    vars_t vars1 = *(vars_t*)ptr1, vars2 = *(vars_t*)ptr2;
    return
        vars1->count == vars2->count &&
        !memcmp(vars1->vars, vars2->vars, sizeof(node_t) * vars1->count);
}

static inline uint32_t hash_vars(const void* ptr) {
    vars_t vars = *(vars_t*)ptr;
    uint32_t h = hash_init();
    for (size_t i = 0, n = vars->count; i < n; ++i)
        h = hash_ptr(h, vars->vars[i]);
    return h;
}

static inline vars_t insert_vars(mod_t mod, vars_t vars) {
    const vars_t* found = find_in_mod_vars(&mod->vars, vars);
    if (found)
        return *found;

    struct vars* new_vars = alloc_from_arena(&mod->arena, sizeof(struct vars));
    new_vars->vars = alloc_from_arena(&mod->arena, sizeof(node_t) * vars->count);
    new_vars->count = vars->count;
    memcpy((node_t*)new_vars->vars, vars->vars, sizeof(node_t) * vars->count);
    vars_t copy = new_vars;
    insert_in_mod_vars(&mod->vars, copy);
    return new_vars;
}

SORT(sort_vars, node_t)

vars_t make_vars(mod_t mod, const node_t* vars, size_t count) {
    node_t* sorted_vars = new_buf(node_t, count);
    memcpy(sorted_vars, vars, sizeof(node_t) * count);
    sort_vars(sorted_vars, count);
#ifndef NDEBUG
    for (size_t i = 1; i < count; ++i)
        assert(sorted_vars[i - 1] < sorted_vars[i]);
#endif
    vars_t res = insert_vars(mod, &(struct vars) { .vars = sorted_vars, .count = count });
    free_buf(sorted_vars);
    return res;
}

vars_t union_vars(mod_t mod, vars_t vars1, vars_t vars2) {
    node_t* vars = new_buf(node_t, vars1->count + vars2->count);
    size_t i = 0, j = 0, count = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            vars[count++] = vars1->vars[i++];
        else if (vars1->vars[i] > vars2->vars[j])
            vars[count++] = vars2->vars[j++];
        else
            vars[count++] = vars1->vars[i++], j++;
    }
    while (i < vars1->count) vars[count++] = vars1->vars[i++];
    while (j < vars2->count) vars[count++] = vars2->vars[j++];
    vars_t res = make_vars(mod, vars, count);
    free_buf(vars);
    return res;
}

vars_t intr_vars(mod_t mod, vars_t vars1, vars_t vars2) {
    size_t min_count = vars1->count < vars2->count ? vars1->count : vars2->count;
    node_t* vars = new_buf(node_t, min_count);
    size_t i = 0, j = 0, count = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            i++;
        else if (vars1->vars[i] > vars2->vars[j])
            j++;
        else
            vars[count++] = vars1->vars[i++], j++;
    }
    vars_t res = make_vars(mod, vars, count);
    free_buf(vars);
    return res;
}

vars_t diff_vars(mod_t mod, vars_t vars1, vars_t vars2) {
    node_t* vars = new_buf(node_t, vars1->count);
    size_t i = 0, j = 0, count = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            vars[count++] = vars1->vars[i++];
        else if (vars1->vars[i] > vars2->vars[j])
            j++;
        else
            i++, j++;
    }
    while (i < vars1->count) vars[count++] = vars1->vars[i++];
    vars_t res = make_vars(mod, vars, count);
    free_buf(vars);
    return res;
}

bool contains_vars(vars_t vars1, vars_t vars2) {
    size_t i = 0, j = 0;
    while (i < vars1->count && j < vars2->count) {
        if (vars1->vars[i] < vars2->vars[j])
            i++;
        else if (vars1->vars[i] > vars2->vars[j])
            j++;
        else
            return true;
    }
    return false;
}

bool contains_var(vars_t vars, node_t var) {
    assert(var->tag == NODE_VAR);
    if (vars->count == 0)
        return false;
    size_t i = 0, j = vars->count - 1;
    while (i <= j) {
        size_t m = (i + j) / 2;
        if (vars->vars[m] < var)
            i = m + 1;
        else if (vars->vars[m] > var) {
            if (m == 0) return false;
            j = m - 1;
        } else
            return true;
    }
    return false;
}

// Labels --------------------------------------------------------------------------

static inline bool compare_label(const void* ptr1, const void* ptr2) {
    label_t label1 = *(label_t*)ptr1, label2 = *(label_t*)ptr2;
    return !strcmp(label1->name, label2->name);
}

static inline uint32_t hash_label(const void* ptr) {
    return hash_str(hash_init(), (*(label_t*)ptr)->name);
}

static inline label_t insert_label(mod_t mod, label_t label) {
    const label_t* found = find_in_mod_labels(&mod->labels, label);
    if (found)
        return *found;

    struct label* new_label = alloc_from_arena(&mod->arena, sizeof(struct label));
    size_t len = strlen(label->name);
    char* name = alloc_from_arena(&mod->arena, len + 1);
    memcpy(name, label->name, len);
    name[len] = 0;

    new_label->name = name;
    new_label->loc = label->loc;

    bool ok = insert_in_mod_labels(&mod->labels, new_label);
    assert(ok); (void)ok;
    return new_label;
}

label_t make_label(mod_t mod, const char* name, const struct loc* loc) {
    return insert_label(mod, &(struct label) {
        .name = name,
        .loc = loc ? *loc : (struct loc) { .file = NULL }
    });
}

size_t find_label(const label_t* labels, size_t label_count, label_t label) {
    for (size_t i = 0; i < label_count; ++i) {
        if (labels[i] == label)
            return i;
    }
    return SIZE_MAX;
}

size_t find_label_in_node(node_t node, label_t label) {
    assert(node->tag == NODE_RECORD || node->tag == NODE_PROD || node->tag == NODE_SUM);
    return find_label(node->record.labels, node->record.arg_count, label);
}

// Expressions ---------------------------------------------------------------------

static inline bool compare_node(const void* ptr1, const void* ptr2) {
    node_t node1 = *(node_t*)ptr1, node2 = *(node_t*)ptr2;
    if (node1->tag != node2->tag || node1->type != node2->type)
        return false;
    switch (node1->tag) {
        case NODE_ERR:
            if (node1->loc.file && node2->loc.file) {
                return
                    node1->loc.begin.col == node2->loc.begin.col &&
                    node1->loc.begin.row == node2->loc.begin.row &&
                    node1->loc.end.col == node2->loc.end.col &&
                    node1->loc.end.row == node2->loc.end.row &&
                    !strcmp(node1->loc.file, node2->loc.file);
            }
            // Both must be NULL
            return node1->loc.file == node2->loc.file;
        case NODE_VAR:
            return node1->var.label == node2->var.label;
        case NODE_UNI:
            return node1->uni.mod == node2->uni.mod;
        case NODE_UNDEF:
        case NODE_STAR:
        case NODE_NAT:
        case NODE_INT:
        case NODE_FLOAT:
        case NODE_TOP:
        case NODE_BOT:
            return true;
        case NODE_LIT:
            return node1->lit.tag == LIT_FLOAT
                ? node1->lit.float_val == node2->lit.float_val
                : node1->lit.int_val  == node2->lit.int_val;
        case NODE_SUM:
        case NODE_PROD:
        case NODE_RECORD:
            return
                node1->record.arg_count == node2->record.arg_count &&
                !memcmp(node1->record.args, node2->record.args, sizeof(node_t) * node1->record.arg_count);
        case NODE_INS:
            return
                node1->ins.val == node2->ins.val &&
                node1->ins.record == node2->ins.record;
        case NODE_EXT:
            return
                node1->ext.val == node2->ext.val &&
                node1->ext.label == node2->ext.label;
        case NODE_ARROW:
            return
                node1->arrow.var == node2->arrow.var &&
                node1->arrow.codom == node2->arrow.codom;
        case NODE_INJ:
            return
                node1->inj.label == node2->inj.label &&
                node1->inj.arg == node2->inj.arg;
        case NODE_FUN:
            return
                node1->fun.var == node2->fun.var &&
                node1->fun.body == node2->fun.body;
        case NODE_APP:
            return
                node1->app.left == node2->app.left &&
                node1->app.right == node2->app.right;
        case NODE_LET:
        case NODE_LETREC:
            return
                node1->let.body == node2->let.body &&
                node1->let.var_count == node2->let.var_count &&
                !memcmp(node1->let.vars, node2->let.vars, sizeof(node_t) * node1->let.var_count) &&
                !memcmp(node1->let.vals, node2->let.vals, sizeof(node_t) * node1->let.var_count);
        case NODE_MATCH:
            return
                node1->match.arg == node2->match.arg &&
                node1->match.pat_count == node2->match.pat_count &&
                !memcmp(node1->match.vals, node2->match.vals, sizeof(node_t) * node1->match.pat_count) &&
                !memcmp(node1->match.pats, node2->match.pats, sizeof(node_t) * node1->match.pat_count);
        default:
            assert(false && "invalid node tag");
            return false;
    }
}

static inline uint32_t hash_node(const void* ptr) {
    node_t node = *(node_t*)ptr;
    uint32_t hash = hash_init();
    hash = hash_uint(hash, node->tag);
    hash = hash_ptr(hash, node->type);
    switch (node->tag) {
        case NODE_UNI:
            hash = hash_ptr(hash, node->uni.mod);
            break;
        case NODE_VAR:
            hash = hash_ptr(hash, node->var.label);
            break;
        case NODE_ERR:
            if (node->loc.file) {
                hash = hash_str(hash, node->loc.file);
                hash = hash_uint(hash, (unsigned)node->loc.begin.row);
                hash = hash_uint(hash, (unsigned)node->loc.begin.col);
                hash = hash_uint(hash, (unsigned)node->loc.end.row);
                hash = hash_uint(hash, (unsigned)node->loc.end.col);
            }
            break;
        default:
            assert(false && "invalid node tag");
            // fallthrough
        case NODE_UNDEF:
        case NODE_STAR:
        case NODE_NAT:
        case NODE_INT:
        case NODE_FLOAT:
        case NODE_TOP:
        case NODE_BOT:
            break;
        case NODE_LIT:
            hash = node->lit.tag == LIT_FLOAT
                ? hash_bytes(hash, &node->lit.float_val, sizeof(node->lit.float_val))
                : hash_uint(hash, node->lit.int_val);
            break;
        case NODE_SUM:
        case NODE_PROD:
        case NODE_RECORD:
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i)
                hash = hash_ptr(hash, node->record.args[i]);
            break;
        case NODE_INS:
            hash = hash_ptr(hash, node->ext.val);
            hash = hash_ptr(hash, node->ins.record);
            break;
        case NODE_EXT:
            hash = hash_ptr(hash, node->ext.val);
            hash = hash_ptr(hash, node->ext.label);
            break;
        case NODE_ARROW:
            hash = hash_ptr(hash, node->arrow.var);
            hash = hash_ptr(hash, node->arrow.codom);
            break;
        case NODE_INJ:
            hash = hash_ptr(hash, node->inj.label);
            hash = hash_ptr(hash, node->inj.arg);
            break;
        case NODE_FUN:
            hash = hash_ptr(hash, node->fun.var);
            hash = hash_ptr(hash, node->fun.body);
            break;
        case NODE_APP:
            hash = hash_ptr(hash, node->app.left);
            hash = hash_ptr(hash, node->app.right);
            break;
        case NODE_LET:
        case NODE_LETREC:
            for (size_t i = 0, n = node->let.var_count; i < n; ++i) {
                hash = hash_ptr(hash, node->let.vars[i]);
                hash = hash_ptr(hash, node->let.vals[i]);
            }
            hash = hash_ptr(hash, node->let.body);
            break;
        case NODE_MATCH:
            for (size_t i = 0, n = node->match.pat_count; i < n; ++i) {
                hash = hash_ptr(hash, node->match.pats[i]);
                hash = hash_ptr(hash, node->match.vals[i]);
            }
            hash = hash_ptr(hash, node->match.arg);
            break;
    }
    return hash;
}

static inline node_t* copy_nodes(mod_t mod, const node_t* nodes, size_t count) {
    node_t* new_nodes = alloc_from_arena(&mod->arena, sizeof(node_t) * count);
    memcpy(new_nodes, nodes, sizeof(node_t) * count);
    return new_nodes;
}

static inline label_t* copy_labels(mod_t mod, const label_t* labels, size_t count) {
    label_t* new_labels = alloc_from_arena(&mod->arena, sizeof(label_t) * count);
    memcpy(new_labels, labels, sizeof(label_t) * count);
    return new_labels;
}

static inline size_t max_depth(node_t node1, node_t node2) {
    return node1->depth > node2->depth ? node1->depth : node2->depth;
}

extern node_t simplify_node(mod_t, node_t);

node_t import_node(mod_t mod, node_t node) {
    node_t* found = find_in_mod_nodes(&mod->nodes, node);
    if (found)
        return *found;

    struct node* new_node = alloc_from_arena(&mod->arena, sizeof(struct node));
    memcpy(new_node, node, sizeof(struct node));
    new_node->free_vars = node->type ? node->type->free_vars : mod->empty_vars;
    new_node->bound_vars = mod->empty_vars;
    new_node->depth = 0;

    // Copy the data contained in the original expression and compute properties
    switch (node->tag) {
        case NODE_SUM:
        case NODE_PROD:
        case NODE_RECORD:
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i) {
                new_node->depth = max_depth(new_node, node->record.args[i]);
                new_node->free_vars = union_vars(mod, new_node->free_vars, node->record.args[i]->free_vars);
                new_node->bound_vars = union_vars(mod, new_node->bound_vars, node->record.args[i]->bound_vars);
            }
            new_node->record.args = copy_nodes(mod, node->record.args, node->record.arg_count);
            new_node->record.labels = copy_labels(mod, node->record.labels, node->record.arg_count);
            break;
        case NODE_INJ:
            new_node->depth = max_depth(new_node, node->inj.arg);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->inj.arg->free_vars);
            new_node->bound_vars = node->inj.arg->bound_vars;
            break;
        case NODE_INS:
            new_node->depth = max_depth(new_node, node->ins.record);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->ins.record->free_vars);
            // fallthrough
        case NODE_EXT:
            new_node->depth = max_depth(new_node, node->ext.val);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->ext.val->free_vars);
            break;
        case NODE_ARROW:
            new_node->depth = max_depth(new_node, node->arrow.codom);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->arrow.codom->free_vars);
            if (!is_unbound_var(new_node->arrow.var))
                new_node->free_vars = diff_vars(mod, new_node->free_vars, make_vars(mod, &node->arrow.var, 1));
            new_node->depth++;
            break;
        case NODE_FUN:
            new_node->depth = max_depth(new_node, node->fun.body) + 1;
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->fun.body->free_vars);
            new_node->free_vars = diff_vars(mod, new_node->free_vars, node->fun.var->bound_vars);
            break;
        case NODE_APP:
            new_node->depth = max_depth(new_node, node->app.left);
            new_node->depth = max_depth(new_node, node->app.right);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->app.left->free_vars);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->app.right->free_vars);
            break;
        case NODE_LET:
        case NODE_LETREC:
            new_node->depth = max_depth(new_node, node->let.body);
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->let.body->free_vars);
            for (size_t i = 0, n = node->let.var_count; i < n; ++i) {
                assert(!is_unbound_var(node->let.vars[i]));
                new_node->depth = max_depth(new_node, node->let.vals[i]);
                new_node->free_vars = union_vars(mod, new_node->free_vars, node->let.vals[i]->free_vars);
            }
            for (size_t i = 0, n = node->let.var_count; i < n; ++i)
                new_node->free_vars = diff_vars(mod, new_node->free_vars, node->let.vars[i]->bound_vars);
            new_node->let.vars = copy_nodes(mod, node->let.vars, node->let.var_count);
            new_node->let.vals = copy_nodes(mod, node->let.vals, node->let.var_count);
            new_node->depth += node->let.var_count;
            break;
        case NODE_MATCH:
            new_node->match.vals = copy_nodes(mod, node->match.vals, node->match.pat_count);
            new_node->match.pats = copy_nodes(mod, node->match.pats, node->match.pat_count);
            for (size_t i = 0, n = node->match.pat_count; i < n; ++i) {
                new_node->depth = max_depth(new_node, node->match.vals[i]);
                new_node->free_vars = union_vars(mod, new_node->free_vars,
                    diff_vars(mod, node->match.vals[i]->free_vars, node->match.pats[i]->bound_vars));
            }
            new_node->free_vars = union_vars(mod, new_node->free_vars, node->match.arg->free_vars);
            new_node->depth += node->match.pat_count;
            break;
        case NODE_VAR:
            new_node->bound_vars = make_vars(mod, (const node_t*)&new_node, is_unbound_var(node) ? 0 : 1);
            new_node->free_vars = union_vars(mod, new_node->free_vars, new_node->bound_vars);
            break;
        default:
            assert(false && "invalid node tag");
            // fallthrough
        case NODE_UNDEF:
        case NODE_ERR:
        case NODE_UNI:
        case NODE_STAR:
        case NODE_NAT:
        case NODE_INT:
        case NODE_FLOAT:
        case NODE_TOP:
        case NODE_BOT:
        case NODE_LIT:
            break;
    }

    assert(!find_in_mod_nodes(&mod->nodes, node));
    node_t res = simplify_node(mod, new_node);
    assert(!find_in_mod_nodes(&mod->nodes, node));
    bool ok = insert_in_mod_nodes(&mod->nodes, new_node, res);
    assert(ok); (void)ok;
    return res;
}

// Module --------------------------------------------------------------------------

mod_t new_mod() {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->arena = new_arena();
    mod->nodes = new_mod_nodes();
    mod->labels = new_mod_labels();
    mod->vars = new_mod_vars();
    mod->empty_vars = make_vars(mod, NULL, 0);

    mod->uni  = import_node(mod, &(struct node) { .tag = NODE_UNI,  .uni.mod = mod });
    mod->star = import_node(mod, &(struct node) { .tag = NODE_STAR, .type = mod->uni });
    mod->nat  = import_node(mod, &(struct node) { .tag = NODE_NAT,  .type = mod->star });
    node_t int_or_float_type = make_non_binding_arrow(mod, mod->nat, mod->star, NULL);
    mod->int_   = import_node(mod, &(struct node) { .tag = NODE_INT,   .type = int_or_float_type });
    mod->float_ = import_node(mod, &(struct node) { .tag = NODE_FLOAT, .type = int_or_float_type });
    return mod;
}

void free_mod(mod_t mod) {
    free_mod_nodes(&mod->nodes);
    free_mod_labels(&mod->labels);
    free_mod_vars(&mod->vars);
    free_arena(mod->arena);
    free(mod);
}

node_t make_uni  (mod_t mod) { return mod->uni; }
node_t make_star (mod_t mod) { return mod->star; }
node_t make_nat  (mod_t mod) { return mod->nat; }
node_t make_int  (mod_t mod) { return mod->int_; }
node_t make_float(mod_t mod) { return mod->float_; }

node_t make_arrow(mod_t mod, node_t var, node_t codom, const struct loc* loc) {
    assert(var->tag == NODE_VAR);
    return import_node(mod, &(struct node) {
        .tag = NODE_ARROW,
        .type = codom->type,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .arrow = { .var = var, .codom = codom }
    });
}

node_t make_fun(mod_t mod, node_t var, node_t body, const struct loc* loc) {
    assert(var->tag == NODE_VAR);
    return import_node(mod, &(struct node) {
        .tag = NODE_FUN,
        .type = make_arrow(mod, var, body->type, loc),
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .fun = { .var = var, .body = body }
    });
}

node_t make_non_binding_arrow(mod_t mod, node_t dom, node_t codom, const struct loc* loc) {
    return make_arrow(mod, make_unbound_var(mod, dom, loc), codom, loc);
}

node_t make_non_binding_fun(mod_t mod, node_t dom, node_t body, const struct loc* loc) {
    return make_fun(mod, make_unbound_var(mod, dom, loc), body, loc);
}

node_t make_untyped_err(mod_t mod, const struct loc* loc) {
    struct node* err = alloc_from_arena(&mod->arena, sizeof(struct node));
    err->tag = NODE_ERR;
    err->type = NULL;
    err->loc = loc ? *loc : (struct loc) { .file = NULL };
    err->depth = 0;
    err->free_vars = mod->empty_vars;
    return err;
}

node_t make_unbound_var(mod_t mod, node_t type, const struct loc* loc) {
    return import_node(mod, &(struct node) {
        .tag = NODE_VAR,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .type = type
    });
}

node_t make_undef(mod_t mod) {
    if (!mod->undef)
        mod->undef = import_node(mod, &(struct node) { .tag = NODE_UNDEF });
    return mod->undef;
}

node_t make_nat_lit(mod_t mod, uintmax_t val, const struct loc* loc) {
    return import_node(mod, &(struct node) {
        .tag = NODE_LIT,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .type = make_nat(mod),
        .lit = { .tag = LIT_INT, .int_val = val }
    });
}

node_t make_int_app(mod_t mod, node_t size, const struct loc* loc) {
    return import_node(mod, &(struct node) {
        .tag = NODE_APP,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .type = make_star(mod),
        .app = { .left = make_int(mod), .right = size }
    });
}

node_t make_float_app(mod_t mod, node_t size, const struct loc* loc) {
    return import_node(mod, &(struct node) {
        .tag = NODE_APP,
        .loc = loc ? *loc : (struct loc) { .file = NULL },
        .type = make_star(mod),
        .app = { .left = make_float(mod), .right = size }
    });
}

vars_t make_empty_vars(mod_t mod) {
    return mod->empty_vars;
}

mod_t get_mod(node_t node) {
    assert(node);
    while (node->tag != NODE_UNI) {
        node = node->type;
        assert(node && "node does not belong to any module");
    }
    return node->uni.mod;
}

// Predicates ----------------------------------------------------------------------

bool is_pat(node_t node) {
    switch (node->tag) {
        case NODE_LIT:  return true;
        case NODE_VAR:  return true;
        case NODE_RECORD:
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i) {
                if (!is_pat(node->record.args[i]))
                    return false;
            }
            return true;
        case NODE_INJ:
            return is_pat(node->inj.arg);
        default:
            return false;
    }
}

bool is_trivial_pat(node_t node) {
    switch (node->tag) {
        case NODE_LIT:  return false;
        case NODE_VAR:  return true;
        case NODE_RECORD:
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i) {
                if (!is_trivial_pat(node->record.args[i]))
                    return false;
            }
            return true;
        default:
            assert(false && "invalid pattern");
            // fallthrough
        case NODE_INJ:
            return false;
    }
}

bool is_unbound_var(node_t var) {
    assert(var->tag == NODE_VAR);
    return var->var.label == NULL;
}

bool is_int_app  (node_t node) { return node->tag == NODE_APP && node->app.left->tag == NODE_INT; }
bool is_float_app(node_t node) { return node->tag == NODE_APP && node->app.left->tag == NODE_FLOAT; }

static bool search_in_node(node_t node, bool (*pred) (node_t)) {
    if (pred(node))
        return true;
    if (node->type && search_in_node(node->type, pred))
        return true;
    switch (node->tag) {
        case NODE_INJ: return search_in_node(node->inj.arg, pred);
        case NODE_EXT: return search_in_node(node->ext.val, pred);
        case NODE_INS:
            return
                search_in_node(node->ins.val, pred) ||
                search_in_node(node->ins.record, pred);
        case NODE_ARROW:
            return
                search_in_node(node->arrow.var, pred) ||
                search_in_node(node->arrow.codom, pred);
        case NODE_FUN:
            return
                search_in_node(node->fun.var, pred) ||
                search_in_node(node->fun.body, pred);
        case NODE_APP:
            return
                search_in_node(node->app.left, pred) ||
                search_in_node(node->app.right, pred);
        case NODE_SUM:
        case NODE_PROD:
        case NODE_RECORD:
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i) {
                if (search_in_node(node->record.args[i], pred))
                    return true;
            }
            return false;
        case NODE_LETREC:
        case NODE_LET:
            for (size_t i = 0, n = node->let.var_count; i < n; ++i) {
                if (search_in_node(node->let.vars[i], pred) ||
                    search_in_node(node->let.vals[i], pred))
                    return true;
            }
            return search_in_node(node->let.body, pred);
        case NODE_MATCH:
            for (size_t i = 0, n = node->match.pat_count; i < n; ++i) {
                if (search_in_node(node->match.pats[i], pred) ||
                    search_in_node(node->match.vals[i], pred))
                    return true;
            }
            return search_in_node(node->match.arg, pred);
        default:
            return false;
    }
}

static bool is_err  (node_t node) { return node->tag == NODE_ERR; }
static bool is_undef(node_t node) { return node->tag == NODE_UNDEF; }

bool has_err  (node_t node) { return search_in_node(node, is_err); }
bool has_undef(node_t node) { return search_in_node(node, is_undef); }

// Rebuild/Replace ----------------------------------------------------------

node_t rebuild_node(node_t node) {
    return import_node(get_mod(node), node);
}

static inline bool needs_replace(node_t node, const node_t* vars, size_t var_count) {
    switch (node->tag) {
        case NODE_UNI:
        case NODE_STAR:
        case NODE_NAT:
        case NODE_INT:
        case NODE_FLOAT:
            return false;
        case NODE_ERR:
            if (node->type == node)
                return false;
            // fallthrough
        default:
            break;
    }
    // Determine if the node depends on the set of variables to replace
    bool needs_replace = false;
    for (size_t i = 0; i < var_count && !needs_replace; ++i)
        needs_replace |= contains_var(node->free_vars, vars[i]);
    return needs_replace;
}

static inline node_t find_replaced(node_t old, struct node_vec* stack, struct node_map* map) {
    node_t new = deref_or_null((void**)find_in_node_map(map, old));
    if (!new)
        push_to_node_vec(stack, old);
    return new;
}

static inline node_t try_replace_vars(node_t node, const node_t* vars, size_t var_count, struct node_vec* stack, struct node_map* map) {
    node_t new_node = deref_or_null((void**)find_in_node_map(map, node));
    if (new_node)
        return new_node;

    if (!needs_replace(node, vars, var_count)) {
        insert_in_node_map(map, node, node);
        return node;
    }

    switch (node->tag) {
        case NODE_ERR:
            assert(node->type != node);
            // fallthrough
        case NODE_TOP:
        case NODE_BOT:
        case NODE_LIT: {
            node_t new_type = find_replaced(node->type, stack, map);
            if (new_type) {
                new_node = rebuild_node(&(struct node) {
                    .tag = node->tag,
                    .type = new_type,
                    .lit = node->lit,
                    .loc = node->loc
                });
            }
            break;
        }
        case NODE_VAR: {
            node_t new_type = find_replaced(node->type, stack, map);
            if (new_type) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = NODE_VAR,
                    .type = new_type,
                    .loc = node->loc,
                    .var.label = node->var.label
                });
            }
            break;
        }
        case NODE_SUM:
        case NODE_PROD:
        case NODE_RECORD: {
            node_t* new_args = new_buf(node_t, node->record.arg_count);
            bool valid = true;
            for (size_t i = 0, n = node->record.arg_count; i < n; ++i)
                valid &= (new_args[i] = find_replaced(node->record.args[i], stack, map)) != NULL;
            if (valid) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = node->tag,
                    .record = {
                        .arg_count = node->record.arg_count,
                        .labels = node->record.labels,
                        .args = new_args
                    },
                    .loc = node->loc
                });
            }
            free_buf(new_args);
            break;
        }
        case NODE_INJ: {
            node_t new_type = find_replaced(node->type, stack, map);
            node_t new_arg = find_replaced(node->inj.arg, stack, map);
            if (new_type && new_arg) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = NODE_INJ,
                    .type = new_type,
                    .loc = node->loc,
                    .inj = { .arg = new_arg, .label = node->inj.label }        
                });
            }
            break;
        }
        case NODE_EXT: {
            node_t new_val = find_replaced(node->ext.val, stack, map);
            node_t new_type = find_replaced(node->type, stack, map);
            if (new_val && new_type) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = NODE_EXT,
                    .type = new_type,
                    .loc = node->loc,
                    .ext = { .val = new_val, .label = node->ext.label }
                });
            }
            break;
        }
        case NODE_INS: {
            node_t new_val = find_replaced(node->ext.val, stack, map);
            node_t new_record = find_replaced(node->ins.record, stack, map);
            node_t new_type = find_replaced(node->type, stack, map);
            if (new_val && new_record && new_type) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = NODE_INS,
                    .type = new_type,
                    .loc = node->loc,
                    .ins = { .val = new_val, .record = new_record }
                });
            }
            break;
        }
        case NODE_ARROW: {
            node_t new_codom = find_replaced(node->arrow.codom, stack, map);
            node_t new_var = find_replaced(node->arrow.var, stack, map);
            if (new_codom && new_var)
                new_node = make_arrow(get_mod(node), new_var, new_codom, &node->loc);
            break;
        }
        case NODE_FUN: {
            node_t new_var = find_replaced(node->fun.var, stack, map);
            node_t new_body = find_replaced(node->fun.body, stack, map);
            if (new_var && new_body)
                new_node = make_fun(get_mod(node), new_var, new_body, &node->loc);
            break;
        }
        case NODE_APP: {
            node_t new_left  = find_replaced(node->app.left, stack, map);
            node_t new_right = find_replaced(node->app.right, stack, map);
            node_t new_type  = find_replaced(node->type, stack, map);
            if (new_left && new_right && new_type) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = NODE_APP,
                    .type = new_type,
                    .loc = node->loc,
                    .app = { .left = new_left, .right = new_right }
                });
            }
            break;
        }
        case NODE_LET:
        case NODE_LETREC: {
            node_t* new_vals = new_buf(node_t, node->let.var_count);
            node_t new_body = find_replaced(node->let.body, stack, map);
            node_t new_type = find_replaced(node->type, stack, map);
            bool valid = new_body && new_type;
            for (size_t i = 0, n = node->let.var_count; i < n; ++i)
                valid &= (new_vals[i] = find_replaced(node->let.vals[i], stack, map)) != NULL;
            if (valid) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = node->tag,
                    .type = new_type,
                    .let = {
                        .vars = node->let.vars,
                        .vals = new_vals,
                        .var_count = node->let.var_count,
                        .body = new_body
                    },
                    .loc = node->loc
                });
            }
            free_buf(new_vals);
            break;
        }
        case NODE_MATCH: {
            node_t* new_vals = new_buf(node_t, node->match.pat_count);
            node_t new_arg = find_replaced(node->match.arg, stack, map);
            node_t new_type = find_replaced(node->type, stack, map);
            bool valid = new_arg && new_type;
            for (size_t i = 0, n = node->match.pat_count; i < n; ++i)
                valid &= (new_vals[i] = find_replaced(node->match.vals[i], stack, map)) != NULL;
            if (valid) {
                new_node = import_node(get_mod(node), &(struct node) {
                    .tag = NODE_MATCH,
                    .type = new_type,
                    .loc = node->loc,
                    .match = {
                        .arg = new_arg,
                        .pats = node->match.pats,
                        .vals = new_vals,
                        .pat_count = node->match.pat_count
                    }
                });
            }
            free_buf(new_vals);
            break;
        }
        default:
            assert(false && "invalid node tag");
            break;
    }
#undef DEPENDS_ON
    if (new_node)
        insert_in_node_map(map, node, new_node);
    return new_node;
}

node_t replace_var(node_t node, node_t from, node_t to) {
    return replace_vars(node, &from, &to, 1);
}

node_t replace_vars(node_t node, const node_t* vars, const node_t* vals, size_t var_count) {
    uint32_t hashes[16];
    node_t keys[ARRAY_SIZE(hashes)];
    node_t values[ARRAY_SIZE(hashes)];
    node_t stack_buf[16];

    struct node_map map = new_node_map_on_stack(ARRAY_SIZE(hashes), keys, hashes, values);
    struct node_vec stack = new_node_vec_on_stack(ARRAY_SIZE(stack_buf), stack_buf);

    push_to_node_vec(&stack, node);
    for (size_t i = 0; i < var_count; ++i)
        insert_in_node_map(&map, vars[i], vals[i]);

    node_t last = NULL;
    while (stack.size > 0) {
        node_t node = stack.elems[stack.size - 1];
        if ((last = try_replace_vars(node, vars, var_count, &stack, &map)))
            pop_from_node_vec(&stack);
    }

    free_node_vec(&stack);
    free_node_map(&map);
    return last;
}

node_t reduce_node(node_t node) {
    bool todo;
    do {
        node_t old_node = node;
        if (node->tag == NODE_FUN)
            return make_fun(get_mod(node), node->fun.var, reduce_node(node->fun.body), &node->loc);
        while (node->tag == NODE_APP) {
            node_t left  = reduce_node(node->app.left);
            node_t right = reduce_node(node->app.right);
            if (left->tag != NODE_FUN) {
                return import_node(get_mod(node), &(struct node) {
                    .tag = NODE_APP,
                    .type = node->type,
                    .loc = node->loc,
                    .app = { .left = left, .right = right }
                });
            }
            node = replace_var(left->fun.body, left->fun.var, right);
        }
        while (node->tag == NODE_LET || node->tag == NODE_LETREC) {
            node_t* new_vals = new_buf(node_t, node->letrec.var_count);
            for (size_t i = 0, n = node->let.var_count; i < n; ++i)
                new_vals[i] = reduce_node(node->let.vals[i]);
            node_t new_body = replace_vars(node->let.body, node->let.vars, new_vals, node->let.var_count);
            node = import_node(get_mod(node), &(struct node) {
                .tag = NODE_LETREC,
                .type = node->type,
                .loc = node->loc,
                .letrec = {
                    .vars = node->letrec.vars,
                    .vals = new_vals,
                    .var_count = node->letrec.var_count,
                    .body = new_body
                }
            });
            free_buf(new_vals);
        }
        todo = old_node != node;
    } while (todo);
    return node;
}
