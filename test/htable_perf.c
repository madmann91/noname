#include <time.h>
#include <stdio.h>

#include "ir/node.h"

int main() {
    mod_t mod = new_mod();
    clock_t t_begin = clock();
    for (size_t k = 0; k < 100; ++k) {
        for (size_t i = 0; i < 100000; ++i) {
            size_t j = i % 9473;
            import_node(mod, &(struct node) {
                .tag = NODE_LIT,
                .type = make_nat(mod),
                .lit = { .tag = LIT_INT, .int_val = j }
            });
        }
    }
    clock_t t_end = clock();
    free_mod(mod);
    printf("%zums\n", ((size_t)t_end - (size_t)t_begin) * 1000 / CLOCKS_PER_SEC);
    return 0;
}
