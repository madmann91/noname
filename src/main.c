#include "exp.h"
#include "print.h"

int main() {
    mod_t mod = new_mod();
    exp_t uni = import_exp(mod, &(struct exp) {
        .tag     = EXP_UNI,
        .uni.mod = mod
    });
    exp_t star = import_exp(mod, &(struct exp) {
        .tag  = EXP_STAR,
        .type = uni
    });
    exp_t t_id = import_exp(mod, &(struct exp) {
        .tag  = EXP_PI,
        .type = uni,
        .pi   = { .dom = star, .codom = star }
    });
    exp_t id_0 = import_exp(mod, &(struct exp) {
        .tag = EXP_BVAR,
        .type = star,
        .bvar = { .index = 0, .sub_index = 0 }
    });
    exp_t id = import_exp(mod, &(struct exp) {
        .tag = EXP_ABS,
        .type = t_id,
        .abs.body = id_0
    });
    exp_t f = import_exp(mod, &(struct exp) {
        .tag = EXP_FVAR,
        .type = star,
        .fvar.name = "f"
    });
    dump_exp(id);
    dump_exp(open_exp(0, id->abs.body, &f, 1));
    free_mod(mod);
    return 0;
}
