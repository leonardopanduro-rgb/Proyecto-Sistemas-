#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

/*
 * Entrada de línea de comandos.
 * argc cuenta argumentos y argv contiene sus cadenas. argv[1] es la carpeta
 * del caso; los siguientes aceptan un máximo de ticks positivo y --render/-r
 * en cualquier orden. main no crea procesos ni toca memoria compartida:
 * delega todo el ciclo concurrente a scheduler_process() y devuelve su estado.
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1 [max_ticks] [--render]\n", argv[0]);
        return 1;
    }

    /* atoi convierte la cadena; solo los valores positivos reemplazan límite. */
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
