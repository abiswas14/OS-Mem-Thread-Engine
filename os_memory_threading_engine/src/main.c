#include "vm.h"
#include "transport.h"

#include <stdio.h>
#include <string.h>

static void usage(const char *program) {
    printf("Usage: %s [vm|transport|all]\n", program);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "vm") == 0) {
        vm_run_demo();
    } else if (strcmp(argv[1], "transport") == 0) {
        rt_run_demo();
    } else if (strcmp(argv[1], "all") == 0) {
        vm_run_demo();
        rt_run_demo();
    } else {
        usage(argv[0]);
        return 1;
    }

    return 0;
}
