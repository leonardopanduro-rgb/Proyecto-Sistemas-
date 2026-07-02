#include <stdio.h>
#include <string.h>

#include "ghost.h"
#include "map.h"
#include "shared.h"

void inicializar_fantasmas_desde_shared(SharedData *shared, GhostState ghosts[]) {
    char simbolos[NUM_GHOSTS] = {'A', 'B', 'C', 'D'};

    for (int i = 0; i < NUM_GHOSTS; i++) {
        ghosts[i].id = i;
        ghosts[i].simbolo = simbolos[i];
        ghosts[i].y = shared->ghost_start_y[i];
        ghosts[i].x = shared->ghost_start_x[i];
    }
}

void mover_fantasma(SharedData *shared, GhostState *ghost, const char *movimiento) {
    int nuevo_y = ghost->y;
    int nuevo_x = ghost->x;

    printf("[Fantasma %c] Intenta movimiento: %s\n",
           ghost->simbolo,
           movimiento);

    if (strcmp(movimiento, "UP") == 0) {
        nuevo_y--;
    } else if (strcmp(movimiento, "DOWN") == 0) {
        nuevo_y++;
    } else if (strcmp(movimiento, "LEFT") == 0) {
        nuevo_x--;
    } else if (strcmp(movimiento, "RIGHT") == 0) {
        nuevo_x++;
    } else if (strncmp(movimiento, "SET_PRIORITY", 12) == 0) {
        printf("[Fantasma %c] SET_PRIORITY será implementado en Checkpoint 10\n",
               ghost->simbolo);
        return;
    } else {
        printf("[Fantasma %c] Movimiento desconocido: %s\n",
               ghost->simbolo,
               movimiento);
        return;
    }

    if (es_celda_valida(shared, nuevo_y, nuevo_x)) {
        ghost->y = nuevo_y;
        ghost->x = nuevo_x;

        printf("[Fantasma %c] Movimiento válido\n", ghost->simbolo);
        printf("[Fantasma %c] Nueva posición: (%d,%d)\n",
               ghost->simbolo,
               ghost->y,
               ghost->x);
    } else {
        printf("[Fantasma %c] Movimiento inválido: pared o límite\n",
               ghost->simbolo);
        printf("[Fantasma %c] Permanece en (%d,%d)\n",
               ghost->simbolo,
               ghost->y,
               ghost->x);
    }
}
