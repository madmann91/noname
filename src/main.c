#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ir/node.h"
#include "utils/log.h"
#include "utils/arena.h"

#define READ_BUF_SIZE 1024
#define ERR_BUF_SIZE  64

static mod_t mod;
static struct log err_log;

static void usage(void) {
    printf(
        "usage: noname [options] files...\n"
        "options:\n"
        "  -h   --help       Prints this message\n"
        "  -e   --execute    Executes the contents of the files\n"
        "       --no-color   Disables colored output\n");
}

struct options {
    size_t file_count;
    bool exec;
};

static bool parse_options(int argc, char** argv, struct options* options) {
    options->file_count = 0;
    options->exec = false;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            options->file_count++;
            continue;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return false;
        } else if (!strcmp(argv[i], "--execute") || !strcmp(argv[i], "-e")) {
            options->exec = true;
        } else if (!strcmp(argv[i], "--no-color")) {
            err_log.out.color = false;
        } else {
            log_error(&err_log, NULL, "unknown option '%0:s'", FORMAT_ARGS({ .s = argv[i] }));
            return false;
        }
    }
    if (options->file_count == 0) {
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

static bool compile_files(int argc, char** argv, const struct options* options, struct log* log) {
    bool status = true;
    struct arena* arena = new_arena();
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-')
            continue;
        size_t size = 0;
        char* data = read_file(argv[i], &size);
        if (!data) {
            log_error(&err_log, NULL, "cannot open file '%0:s'", FORMAT_ARGS({ .s = argv[i] }));
            goto error;
        }

        reset_arena(&arena);
        node_t node = parse_node(mod, &arena, log, argv[i], data, size);
        if (!log->errors)
            node = check_node(mod, log, node);
        if (!log->errors)
            dump_node(node);
        if (options->exec) {
            node = reduce_node(node);
            dump_node(node);
        }
        free(data);
    }
    goto end;
error:
    status = false;
end:
    free_arena(arena);
    return status;
}

int main(int argc, char** argv) {
    int status = EXIT_SUCCESS;
    char err_data[ERR_BUF_SIZE];
    struct format_buf err_buf = {
        .data = err_data,
        .cap  = sizeof(err_data),
    };
    err_log.out.buf = &err_buf;
    err_log.out.color = is_color_supported(stderr);
    err_log.out.tab = "  ";
    err_log.out.indent = 0;
    mod = new_mod();

    struct options options;
    if (!parse_options(argc, argv, &options))
        goto failure;

    if (!compile_files(argc, argv, &options, &err_log))
        goto failure;
    goto success;

failure:
    status = EXIT_FAILURE;
success:
    free_mod(mod);
    dump_format_buf(&err_buf, stderr);
    free_format_buf(err_buf.next);
    return status;
}
