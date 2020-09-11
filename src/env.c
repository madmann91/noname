#include "env.h"
#include "vec.h"

struct env {
    exp_t*  exps;
    size_t* levels;
};

env_t new_env(void) {
    env_t env   = xmalloc(sizeof(struct env));
    env->exps   = NEW_VEC(exp_t);
    env->levels = NEW_VEC(size_t);
    return env;
}

void free_env(env_t env) {
    FREE_VEC(env->exps);
    FREE_VEC(env->levels);
    free(env);
}

size_t get_env_level(env_t env) {
    return VEC_SIZE(env->levels);
}

size_t get_env_size(env_t env) {
    return VEC_SIZE(env->exps);
}

void push_env_level(env_t env) {
    push_env_level_from(env, get_env_size(env));
}

void push_env_level_from(env_t env, size_t size) {
    VEC_PUSH(env->levels, size);
}

void add_exp_to_env(env_t env, exp_t exp) {
    VEC_PUSH(env->exps, exp);
}

void pop_env_level(env_t env) {
    assert(VEC_SIZE(env->levels) > 0);
    size_t last = env->levels[VEC_SIZE(env->levels) - 1];
    RESIZE_VEC(env->exps, last);
    VEC_POP(env->levels);
}

exp_t get_exp_from_env(env_t env, size_t index, size_t sub_index) {
    if (VEC_SIZE(env->levels) < index + 1)
        return NULL;
    size_t last  = VEC_SIZE(env->levels) - index - 1;
    size_t begin = env->levels[last];
    size_t end   = index == 0 ? VEC_SIZE(env->exps) : env->levels[last + 1];
    if (sub_index >= end - begin)
        return NULL;
    return shift_exp(0, env->exps[begin + sub_index], index + 1, true);
}
