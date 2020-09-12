#include <assert.h>
#include "check.h"
#include "env.h"
#include "format.h"
#include "utils.h"

struct checker {
    struct log* log;
    env_t env;
};

checker_t new_checker(struct log* log) {
    checker_t checker = xmalloc(sizeof(struct checker));
    checker->log = log;
    checker->env = new_env();
    return checker;
}

void free_checker(checker_t checker) {
    free_env(checker->env);
    free(checker);
}

static inline bool expect_type(checker_t checker, const struct loc* loc, exp_t type, exp_t expected) {
    if (type != expected) {
        log_error(
            checker->log, loc,
            "expected type '%0:e', but got '%1:e'",   
            FMT_ARGS({ .e = type }, { .e = expected }));
        return false;
    }
    return true;
}

bool check_exp(checker_t checker, exp_t exp) {
    switch (exp->tag) {
        case EXP_LET: {
            exp_t body_type =
                shift_exp(0,
                    open_exp(0, exp->let.body->type, exp->let.binds, exp->let.bind_count),
                    1, false);
            return expect_type(checker, &exp->let.body->loc, body_type, exp->type);
        }
        default:
            assert(false);
            return false;
    }
}
