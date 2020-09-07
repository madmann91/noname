#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "exp.h"
#include "log.h"
#include "print.h"
#include "parse.h"
#include "utils.h"

#ifndef NDEBUG
#define READ_BUF_SIZE 1024
#else
#define READ_BUF_SIZE 1
#endif

struct options {
    int empty;
};

static mod_t mod;
static log_t err_log;
static struct options options;

static void usage(void) {
    printf("usage: noname [options] files...\n");
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
        } else {
            print_msg(err_log, MSG_ERR, NULL, "unknown option '%0:s'", FMT_ARGS({ .s = argv[i] }));
            return false;
        }
    }
    if (file_count == 0) {
        print_msg(err_log, MSG_ERR, NULL, "no input file", NULL);
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
            print_msg(err_log, MSG_ERR, NULL, "cannot open file '%0:s'", FMT_ARGS({ .s = argv[i] }));
            return false;
        }

        parser_t parser = new_parser(mod, err_log, argv[i], data, size);
        exp_t exp = parse_exp(parser);
        if (exp) {
            dump_exp(exp);
            if (exp->type) {
                printf("\n: ");
                dump_exp(exp->type);
            }
        }

        free_parser(parser);
        free(data);
    }
    return true;
}

int main(int argc, char** argv) {
    int status = EXIT_SUCCESS;
    char err_data[PRINT_BUF_SIZE];
    struct fmtbuf err_buf = {
        .data = err_data,
        .cap  = sizeof(err_data),
    };
    err_log = new_log(&err_buf);
    mod = new_mod();

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
    free_log(err_log);
    return status;
}
