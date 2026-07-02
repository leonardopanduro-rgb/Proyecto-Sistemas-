#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1 [max_ticks] [--render]\n", argv[0]);
        return 1;
    }

    /*
        Punto 10: max_ticks puede recibirse como argumento numerico.
        BONUS: --render (o -r) habilita el proceso P3 (renderer).
        Ambos son opcionales y pueden ir en cualquier orden despues del
        caso. Sin --render, el sistema se comporta como la version base.
    */
    int max_ticks_arg = -1;
    int render_enabled = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--render") == 0 || strcmp(argv[i], "-r") == 0) {
            render_enabled = 1;
        } else {
            int valor = atoi(argv[i]);

            if (valor > 0) {
                max_ticks_arg = valor;
            } else {
                printf("[P0] Argumento invalido '%s'; se ignora\n", argv[i]);
            }
        }
    }

    return scheduler_process(argv[1], max_ticks_arg, render_enabled);
}
