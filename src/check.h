#ifndef CHECK_H
#define CHECK_H

#include "log.h"
#include "exp.h"

typedef struct checker* checker_t;

checker_t new_checker(struct log*);
void free_checker(checker_t);
bool check_exp(checker_t, exp_t);

#endif
