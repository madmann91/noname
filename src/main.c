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
    char data[1024];
    struct fmtbuf buf = { .data = data, .cap = sizeof(data) };
    struct printer printer = {
        .buf    = &buf,
        .tab    = "  ",
        .color  = true,
        .indent = 0
    };
    print_exp(&printer, id);
    format(&printer.buf, "\n", NULL);
    print_exp(&printer, open_exp(0, id->abs.body, &f, 1));
    format(&printer.buf, "\n", NULL);
    fwrite(buf.data, 1, buf.size, stdout);
    dump_buf(buf.next, stdout);
    fflush(stdout);
    free_mod(mod);
    return 0;
}
