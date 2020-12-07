#include <stdio.h>

#include "utils/set.h"
#include "utils/hash.h"

SET(int_set, size_t)

int main() {
    struct int_set int_set = new_int_set();
    int status = 0;
    for (size_t i = 0; i < 1000; ++i) {
        if (!insert_in_int_set(&int_set, i)) {
            printf("failed after %zu insertion(s)\n", i);
            status = 1;
            goto cleanup;
        }
    }
    for (size_t i = 0; i < 1000; ++i) {
        if (!find_in_int_set(&int_set, i)) {
            printf("failed after %zu lookup(s)\n", i);
            status = 1;
            goto cleanup;
        }
    }
    for (size_t i = 0; i < 1000; ++i) {
        if (!remove_from_int_set(&int_set, i)) {
            printf("failed after %zu removal(s)\n", i);
            status = 1;
            goto cleanup;
        }
    }
    status = int_set.htable.size == 0 ? 0 : 1;
cleanup:
    free_int_set(&int_set);
    return status;
}
