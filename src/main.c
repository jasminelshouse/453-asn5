#include <stdio.h>
#include <string.h>
#include "shared.h"

int minget_main(int argc, char *argv[]);
int minls_main(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: Missing subcommand (minget or minls).\n");
        return 1;
    }

    if (strcmp(argv[1], "minget") == 0) {
        return minget_main(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "minls") == 0) {
        return minls_main(argc - 1, &argv[1]);
    } else {
        return 1;
    }
}
