#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

#define CC "cc"
#define TARGET "tgf"

const char *srcs[] = {
    "src/main.c",
    "src/storage.c",
    "src/handlers.c",
    "src/network.c",
    "src/config.c",
    "src/utils.c",
    "vendor/cJSON.c",
    "vendor/toml.c",
    "vendor/sha256.c",
};

const char *cflags[] = {
    "-Wall", "-Wextra", "-std=c11",
    "-I./include", "-I./vendor",
    "-O3", "-D_GNU_SOURCE", "-D_DEFAULT_SOURCE"
};

const char *ldflags[] = {
    "-luv", "-lsqlite3", "-ltdjson", "-lm", "-lpthread"
};

int main(int argc, char **argv) {
    NOB_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, CC);

    for (size_t i = 0; i < NOB_ARRAY_LEN(cflags); i++) {
        nob_cmd_append(&cmd, cflags[i]);
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(srcs); i++) {
        nob_cmd_append(&cmd, srcs[i]);
    }

    nob_cmd_append(&cmd, "-o", TARGET);

    for (size_t i = 0; i < NOB_ARRAY_LEN(ldflags); i++) {
        nob_cmd_append(&cmd, ldflags[i]);
    }

    if (!nob_cmd_run_sync(cmd)) {
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "run") == 0) {
        Nob_Cmd run_cmd = {0};
        nob_cmd_append(&run_cmd, "./" TARGET);
        nob_cmd_run_sync(run_cmd);
    }

    if (argc > 1 && strcmp(argv[1], "clean") == 0) {
        cmd_append(&cmd, "rm -f ", TARGET);
    }

    return 0;
}
