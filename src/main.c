#include <stdio.h>

#include "scheduler.h"

int main(int argc, char *argv[]) {
    const char *case_dir = "cases/Caso1";

    if (argc > 1) {
        case_dir = argv[1];
    }

    printf("Pac-Man concurrente POSIX - avance base\n");
    printf("Caso seleccionado: %s\n", case_dir);

    return scheduler_process_main(case_dir);
}

