#ifndef ENV_H
#define ENV_H

#include "exp.h"

typedef struct env* env_t;

env_t new_env(void);
void free_env(env_t);
size_t get_env_level(env_t);
size_t get_env_size(env_t);
void push_env_level(env_t);
void push_env_level_from(env_t, size_t);
void add_exp_to_env(env_t, exp_t);
void pop_env_level(env_t);
exp_t get_exp_from_env(env_t, size_t, size_t);

#endif
