#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "exp.h"
#include "utils.h"
#include "htable.h"
#include "arena.h"
#include "hash.h"

struct mod {
    struct htable exps, pats, strs;
    arena_t arena;
};

static bool cmp_str(const void* ptr1, const void* ptr2) {
    return !strcmp(*(const char**)ptr1, *(const char**)ptr2);
}

static inline uint32_t hash_str(const char* str) {
    return hash_bytes(FNV_OFFSET, str, strlen(str));
}

static inline bool cmp_lit(exp_t type, const union lit* lit1, const union lit* lit2) {
    return type->tag == EXP_REAL
        ? lit1->real_val == lit2->real_val
        : lit1->int_val  == lit2->int_val;
}

static inline uint32_t hash_lit(uint32_t hash, exp_t type, const union lit* lit) {
    return type->tag == EXP_REAL
        ? hash_bytes(hash, &lit->real_val, sizeof(lit->real_val))
        : hash_uint(hash, lit->int_val);
}

static bool cmp_exp(const void* ptr1, const void* ptr2) {
    exp_t exp1 = *(exp_t*)ptr1, exp2 = *(exp_t*)ptr2;
    if (exp1->tag != exp2->tag || exp1->type != exp2->type)
        return false;
    switch (exp1->tag) {
        case EXP_BVAR:
            return
                exp1->bvar.index == exp2->bvar.index &&
                exp1->bvar.sub_index == exp2->bvar.index;
        case EXP_FVAR:
            return exp1->fvar.name == exp2->fvar.name;
        case EXP_UNI:
            return exp1->uni.mod == exp2->uni.mod;
        case EXP_TOP:
        case EXP_BOT:
        case EXP_STAR:
            return true;
        case EXP_INT:
        case EXP_REAL:
            return exp1->real.bitwidth == exp2->real.bitwidth;
        case EXP_LIT:
            return cmp_lit(exp1->type, &exp1->lit, &exp2->lit);
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            return
                exp1->tup.arg_count == exp2->tup.arg_count &&
                !memcmp(exp1->tup.args, exp2->tup.args, sizeof(exp_t) * exp1->tup.arg_count);
        case EXP_PI:
            return
                exp1->pi.dom == exp2->pi.dom &&
                exp1->pi.codom == exp2->pi.codom;
        case EXP_INJ:
            return
                exp1->inj.index == exp2->inj.index &&
                exp1->inj.arg == exp2->inj.arg;
        case EXP_ABS:
            return exp1->abs.body == exp2->abs.body;
        case EXP_APP:
            return
                exp1->app.left == exp2->app.left &&
                exp1->app.right == exp2->app.right;
        case EXP_LET:
            return
                exp1->let.body == exp2->let.body &&
                exp1->let.bind_count == exp2->let.bind_count &&
                !memcmp(exp1->let.binds, exp2->let.binds, sizeof(exp_t) * exp1->let.bind_count);
        case EXP_MATCH:
            return
                exp1->match.arg == exp2->match.arg &&
                exp1->match.pat_count == exp2->match.pat_count &&
                !memcmp(exp1->match.exps, exp2->match.exps, sizeof(exp_t) * exp1->match.pat_count) &&
                !memcmp(exp1->match.pats, exp2->match.pats, sizeof(pat_t) * exp1->match.pat_count);
        default:
            assert(false && "invalid expression tag");
            return false;
    }
}

static inline uint32_t hash_exp(exp_t exp) {
    uint32_t hash = FNV_OFFSET;
    hash = hash_uint(hash, exp->tag);
    hash = hash_ptr(hash, exp->type);
    switch (exp->tag) {
        case EXP_BVAR:
            hash = hash_uint(hash, exp->bvar.index);
            hash = hash_uint(hash, exp->bvar.sub_index);
            break;
        case EXP_FVAR:
            hash = hash_ptr(hash, exp->fvar.name);
            break;
        case EXP_UNI:
            hash = hash_ptr(hash, exp->uni.mod);
            break;
        default:
            assert(false && "invalid expression tag");
            // fallthrough
        case EXP_STAR:
        case EXP_TOP:
        case EXP_BOT:
            break;
        case EXP_INT:
        case EXP_REAL:
            hash = hash_ptr(hash, exp->real.bitwidth);
            break;
        case EXP_LIT:
            hash = hash_lit(hash, exp->type, &exp->lit);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                hash = hash_ptr(hash, exp->tup.args[i]);
            break;
        case EXP_PI:
            hash = hash_ptr(hash, exp->pi.dom);
            hash = hash_ptr(hash, exp->pi.codom);
            break;
        case EXP_INJ:
            hash = hash_uint(hash, exp->inj.index);
            hash = hash_ptr(hash, exp->inj.arg);
            break;
        case EXP_ABS:
            hash = hash_ptr(hash, exp->abs.body);
            break;
        case EXP_APP:
            hash = hash_ptr(hash, exp->app.left);
            hash = hash_ptr(hash, exp->app.right);
            break;
        case EXP_LET:
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i)
                hash = hash_ptr(hash, exp->let.binds[i]);
            hash = hash_ptr(hash, exp->let.body);
            break;
        case EXP_MATCH:
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i) {
                hash = hash_ptr(hash, exp->match.pats[i]);
                hash = hash_ptr(hash, exp->match.exps[i]);
            }
            hash = hash_ptr(hash, exp->match.arg);
            break;
    }
    return hash;
}

static bool cmp_pat(const void* ptr1, const void* ptr2) {
    pat_t pat1 = *(pat_t*)ptr1, pat2 = *(pat_t*)ptr2;
    if (pat1->tag != pat2->tag || pat1->type != pat2->type)
        return false;
    switch (pat1->tag) {
        case EXP_BVAR:
            return pat1->bvar.index == pat2->bvar.index;
        case EXP_FVAR:
            return pat1->fvar.name == pat2->fvar.name;
        case PAT_LIT:
            return cmp_lit(pat1->type, &pat1->lit, &pat2->lit);
        case PAT_TUP:
            return
                pat1->tup.arg_count == pat2->tup.arg_count &&
                !memcmp(pat1->tup.args, pat2->tup.args, sizeof(pat_t) * pat1->tup.arg_count);
        case PAT_INJ:
            return
                pat1->inj.index == pat2->inj.index &&
                pat1->inj.arg == pat2->inj.arg;
        default:
            assert(false && "invalid pattern tag");
            return false;
    }
}

static inline uint32_t hash_pat(pat_t pat) {
    uint32_t hash = FNV_OFFSET;
    hash = hash_uint(hash, pat->tag);
    hash = hash_ptr(hash, pat->type);
    switch (pat->tag) {
        case EXP_BVAR:
            hash = hash_uint(hash, pat->bvar.index);
            break;
        case EXP_FVAR:
            hash = hash_ptr(hash, pat->fvar.name);
            break;
        case PAT_LIT:
            hash = hash_lit(hash, pat->type, &pat->lit);
            break;
        case PAT_TUP:
            for (size_t i = 0, n = pat->tup.arg_count; i < n; ++i)
                hash = hash_ptr(hash, pat->tup.args[i]);
            break;
        case PAT_INJ:
            hash = hash_uint(hash, pat->inj.index);
            hash = hash_ptr(hash, pat->inj.arg);
            break;
        default:
            assert(false && "invalid pattern tag");
            break;
    }
    return hash;
}

mod_t new_mod(void) {
    mod_t mod = xmalloc(sizeof(struct mod));
    mod->exps = new_htable(sizeof(exp_t), DEFAULT_CAP, cmp_exp);
    mod->pats = new_htable(sizeof(pat_t), DEFAULT_CAP, cmp_pat);
    mod->strs = new_htable(sizeof(const char*), DEFAULT_CAP, cmp_str);
    mod->arena = new_arena(DEFAULT_ARENA_SIZE);
    return mod;
}

void free_mod(mod_t mod) {
    free_htable(&mod->exps);
    free_htable(&mod->pats);
    free_htable(&mod->strs);
    free_arena(mod->arena);
    free(mod);
}

mod_t get_mod_from_exp(exp_t exp) {
    while (exp->tag != EXP_UNI)
        exp = exp->type;
    return exp->uni.mod;
}

mod_t get_mod_from_pat(pat_t pat) {
    return get_mod_from_exp(pat->type);
}

exp_t rebuild_exp(exp_t exp) {
    return import_exp(get_mod_from_exp(exp), exp);
}

pat_t rebuild_pat(pat_t pat) {
    return import_pat(get_mod_from_pat(pat), pat);
}

static inline exp_t* copy_exps(mod_t mod, const exp_t* exps, size_t count) {
    exp_t* new_exps = alloc_in_arena(&mod->arena, sizeof(exp_t) * count);
    memcpy(new_exps, exps, sizeof(exp_t) * count);
    return new_exps;
}

static inline pat_t* copy_pats(mod_t mod, const pat_t* pats, size_t count) {
    pat_t* new_pats = alloc_in_arena(&mod->arena, sizeof(pat_t) * count);
    memcpy(new_pats, pats, sizeof(pat_t) * count);
    return new_pats;
}

static inline const char* import_str(mod_t mod, const char* str) {
    uint32_t hash = hash_str(str);
    const char** found = find_in_htable(&mod->exps, &str, hash);
    if (found)
        return *found;

    size_t len = strlen(str);
    char* new_str = alloc_in_arena(&mod->arena, len + 1);
    strcpy(new_str, str);

    const char* copy = new_str;
    bool ok = insert_in_htable(&mod->strs, &copy, hash, NULL);
    assert(ok); (void)ok;
    return new_str;
}

exp_t import_exp(mod_t mod, exp_t exp) {
    uint32_t hash = hash_exp(exp);
    exp_t* found = find_in_htable(&mod->exps, &exp, hash);
    if (found)
        return *found;

    struct exp* new_exp = alloc_in_arena(&mod->arena, sizeof(struct exp));
    memcpy(new_exp, exp, sizeof(struct exp));

    // Copy the data contained in the original expression
    switch (exp->tag) {
        case EXP_FVAR:
            new_exp->fvar.name = import_str(mod, exp->fvar.name);
            break;
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP:
            new_exp->tup.args = copy_exps(mod, exp->tup.args, exp->tup.arg_count);
            break;
        case EXP_LET:
            new_exp->let.binds = copy_exps(mod, exp->let.binds, exp->let.bind_count);
            break;
        case EXP_MATCH:
            new_exp->match.exps = copy_exps(mod, exp->match.exps, exp->match.pat_count);
            new_exp->match.pats = copy_pats(mod, exp->match.pats, exp->match.pat_count);
            break;
        default:
            break;
    }

    exp_t copy = new_exp;
    bool ok = insert_in_htable(&mod->exps, &copy, hash, NULL);
    assert(ok); (void)ok;
    return new_exp;
}

pat_t import_pat(mod_t mod, pat_t pat) {
    uint32_t hash = hash_pat(pat);
    pat_t* found = find_in_htable(&mod->pats, &pat, hash);
    if (found)
        return *found;

    struct pat* new_pat = alloc_in_arena(&mod->arena, sizeof(struct pat));
    memcpy(new_pat, pat, sizeof(struct pat));

    // Copy the data contained in the original pattern
    if (pat->tag == PAT_TUP)
        new_pat->tup.args = copy_pats(mod, pat->tup.args, pat->tup.arg_count);
    else if (pat->tag == PAT_FVAR)
        new_pat->fvar.name = import_str(mod, pat->fvar.name);

    pat_t copy = new_pat;
    bool ok = insert_in_htable(&mod->pats, &copy, hash, NULL);
    assert(ok); (void)ok;
    return new_pat;
}

static exp_t open_or_close_exp(bool open, size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    switch (exp->tag) {
        case EXP_BVAR:
            if (open && exp->bvar.index == index) {
                assert(exp->bvar.sub_index < fv_count);
                return fvs[exp->bvar.sub_index];
            }
            break;
        case EXP_FVAR:
            if (!open) {
                for (size_t i = 0; i < fv_count; ++i) {
                    if (exp == fvs[i]) {
                        return rebuild_exp(&(struct exp) {
                            .tag  = EXP_BVAR,
                            .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                            .bvar = {
                                .index = index,
                                .sub_index = i
                            }
                        });
                    }
                }
            }
            break;
        case EXP_UNI:
        case EXP_STAR:
            return exp;
        default:
            assert(false && "invalid expression tag");
            // fallthrough
        case EXP_TOP:
        case EXP_BOT:
        case EXP_LIT:
            break;
        case EXP_PI:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_PI,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .pi   = {
                    .dom   = open_or_close_exp(open, index, exp->pi.dom, fvs, fv_count),
                    .codom = open_or_close_exp(open, index + 1, exp->pi.codom, fvs, fv_count)
                }
            });
        case EXP_SUM:
        case EXP_PROD:
        case EXP_TUP: {
            NEW_BUF(new_args, exp_t, exp->tup.arg_count)
            for (size_t i = 0, n = exp->tup.arg_count; i < n; ++i)
                new_args[i] = open_or_close_exp(open, index, exp->tup.args[i], fvs, fv_count);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag  = exp->tag,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .tup  = {
                    .args      = new_args,
                    .arg_count = exp->tup.arg_count
                }
            });
            FREE_BUF(new_args);
            return new_exp;
        }
        case EXP_INJ:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_INJ,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .inj  = {
                    .arg   = open_or_close_exp(open, index, exp->inj.arg, fvs, fv_count),
                    .index = exp->inj.index
                }
            });
        case EXP_ABS:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_ABS,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .abs.body = open_or_close_exp(open, index + 1, exp->abs.body, fvs, fv_count)
            });
        case EXP_APP:
            return rebuild_exp(&(struct exp) {
                .tag  = EXP_APP,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .app  = {
                    .left  = open_or_close_exp(open, index + 1, exp->app.left, fvs, fv_count),
                    .right = open_or_close_exp(open, index + 1, exp->app.right, fvs, fv_count)
                }
            });
        case EXP_LET: {
            NEW_BUF(new_binds, exp_t, exp->let.bind_count)
            for (size_t i = 0, n = exp->let.bind_count; i < n; ++i)
                new_binds[i] = open_or_close_exp(open, index + 1, exp->let.binds[i], fvs, fv_count);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag  = EXP_LET,
                .type = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .let  = {
                    .binds      = new_binds,
                    .bind_count = exp->let.bind_count,
                    .body       = open_or_close_exp(open, index + 1, exp->let.body, fvs, fv_count)
                }
            });
            FREE_BUF(new_binds);
            return new_exp;
        }
        case EXP_MATCH: {
            NEW_BUF(new_exps, exp_t, exp->match.pat_count)
            for (size_t i = 0, n = exp->match.pat_count; i < n; ++i)
                new_exps[i] = open_or_close_exp(open, index + 1, exp->match.exps[i], fvs, fv_count);
            exp_t new_exp = rebuild_exp(&(struct exp) {
                .tag   = EXP_MATCH,
                .type  = open_or_close_exp(open, index, exp->type, fvs, fv_count),
                .match = {
                    .arg       = open_or_close_exp(open, index + 1, exp->match.arg, fvs, fv_count),
                    .pats      = exp->match.pats,
                    .exps      = new_exps,
                    .pat_count = exp->match.pat_count
                }
            });
            FREE_BUF(new_exps);
            return new_exp;
        }
    }

    exp_t new_type = open_or_close_exp(open, index, exp->type, fvs, fv_count);
    if (new_type == exp->type)
        return exp;
    struct exp copy = *exp;
    copy.type = new_type;
    return rebuild_exp(&copy);
}

exp_t open_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    return open_or_close_exp(true, index, exp, fvs, fv_count);
}

exp_t close_exp(size_t index, exp_t exp, exp_t* fvs, size_t fv_count) {
    return open_or_close_exp(false, index, exp, fvs, fv_count);
}
