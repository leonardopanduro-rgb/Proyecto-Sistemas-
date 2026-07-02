#include <stdio.h>
#include <string.h>

#include "pacman.h"
#include "map.h"
#include "shared.h"

void mover_pacman(SharedData *shared, const char *movimiento) {
    int nuevo_y = shared->pacman_y;
    int nuevo_x = shared->pacman_x;

    printf("[Pac-Man] Intenta movimiento: %s\n", movimiento);

    if (strcmp(movimiento, "UP") == 0) {
        nuevo_y--;
    } else if (strcmp(movimiento, "DOWN") == 0) {
        nuevo_y++;
    } else if (strcmp(movimiento, "LEFT") == 0) {
        nuevo_x--;
    } else if (strcmp(movimiento, "RIGHT") == 0) {
        nuevo_x++;
    } else if (strncmp(movimiento, "SET_PRIORITY", 12) == 0) {
        printf("[Pac-Man] SET_PRIORITY será implementado en Checkpoint 10\n");
        return;
    } else {
        printf("[Pac-Man] Movimiento desconocido: %s\n", movimiento);
        return;
    }

    if (es_celda_valida(shared, nuevo_y, nuevo_x)) {
        shared->pacman_y = nuevo_y;
        shared->pacman_x = nuevo_x;
        shared->pacman_score += 10;

        printf("[Pac-Man] Movimiento válido\n");
        printf("[Pac-Man] Nueva posición: (%d,%d)\n",
               shared->pacman_y,
               shared->pacman_x);
        printf("[Pac-Man] Puntaje: %d\n", shared->pacman_score);
    } else {
        printf("[Pac-Man] Movimiento inválido: pared o límite\n");
        printf("[Pac-Man] Permanece en (%d,%d)\n",
               shared->pacman_y,
               shared->pacman_x);
    }
}
