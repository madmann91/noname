#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ir/exp.h"
#include "ir/print.h"
#include "ir/parse.h"
#include "lang/ast.h"
#include "utils/log.h"

#define READ_BUF_SIZE 1024
#define ERR_BUF_SIZE  64

static mod_t mod;
static struct log err_log;

static void usage(void) {
    printf(
        "usage: noname [options] files...\n"
        "options:\n"
        "  -h   --help       Prints this message\n"
        "       --no-color   Disables colored output\n");
}

static bool parse_options(int argc, char** argv) {
    size_t file_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            file_count++;
            continue;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return false;
        } else if (!strcmp(argv[i], "--no-color")) {
            err_log.color = false;
        } else {
            log_error(&err_log, NULL, "unknown option '%0:s'", FMT_ARGS({ .s = argv[i] }));
            return false;
        }
    }
    if (file_count == 0) {
        log_error(&err_log, NULL, "no input file", NULL);
        return false;
    }
    return true;
}

static char* read_file(const char* name, size_t* size) {
    FILE* fp = fopen(name, "r");
    if (!fp)
        return NULL;
    char* data = xmalloc(READ_BUF_SIZE);
    size_t cap = READ_BUF_SIZE;
    *size = 0;
    while (true) {
        size_t avail = cap - *size;
        size_t read  = fread(data + *size, 1, avail, fp);
        *size += read;
        if (read < avail)
            break;
        cap  = cap + cap / 2 + 1;
        data = xrealloc(data, cap * 2);
    }
    fclose(fp);
    return data;
}

static bool compile_files(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-')
            continue;
        size_t size = 0;
        char* data = read_file(argv[i], &size);
        if (!data) {
            log_error(&err_log, NULL, "cannot open file '%0:s'", FMT_ARGS({ .s = argv[i] }));
            return false;
        }

        if (data[0] != '(') {
            struct arena* arena = new_arena();
            parse_ast(&arena, &err_log, argv[i], data, size);
            free_arena(arena);
        } else {
            exp_t exp = parse_exp(mod, &err_log, argv[i], data, size);
            if (exp) {
                dump_exp(exp);
                while (true) {
                    exp = exp->type;
                    if (!exp)
                        break;
                    printf(": ");
                    dump_exp(exp);
                }
            }
        }
        free(data);
    }
    return true;
}

int main(int argc, char** argv) {
    int status = EXIT_SUCCESS;
    char err_data[ERR_BUF_SIZE];
    struct fmtbuf err_buf = {
        .data = err_data,
        .cap  = sizeof(err_data),
    };
    err_log.buf   = &err_buf;
    err_log.color = is_color_supported(stderr);
    mod = new_mod(&err_log);

    if (!parse_options(argc, argv))
        goto failure;

    if (!compile_files(argc, argv))
        goto failure;
    goto success;

failure:
    status = EXIT_FAILURE;
success:
    free_mod(mod);
    dump_fmtbuf(&err_buf, stderr);
    free_fmtbuf(err_buf.next);
    return status;
}
