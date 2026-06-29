#include "collision.h"
#include "map.h"
#include "utils.h"

/*
    Verifica si Pac-Man está en la misma posición que algún fantasma.

    Retorna:
    1 si hubo colisión.
    0 si no hubo colisión.
*/
int verificar_colision() {
    int i;

    for (i = 0; i < 4; i++) {
        if (ghost_x[i] == -1 || ghost_y[i] == -1) {
            continue;
        }

        if (pacman_x == ghost_x[i] && pacman_y == ghost_y[i]) {
            escribir_texto("\nCOLISION DETECTADA con Fantasma ");
            escribir_numero(i + 1);
            escribir_texto("\n");

            pacman_lives--;

            escribir_texto("Vidas restantes: ");
            escribir_numero(pacman_lives);
            escribir_texto("\n");

            return 1;
        }
    }

    return 0;
}

void imprimir_vidas() {
    escribir_texto("Vidas de Pac-Man: ");
    escribir_numero(pacman_lives);
    escribir_texto("\n");
}