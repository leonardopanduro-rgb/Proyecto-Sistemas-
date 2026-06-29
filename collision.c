#include <stdio.h>

#include "collision.h"
#include "shared.h"
#include "ghost.h"

/*
    Esta función pertenece a la lógica de P2.

    Según el PDF:
    P2 detecta la colisión y publica el evento.
    P0 es quien baja vidas y decide game_over.
*/
int verificar_colision(SharedData *shared, GhostState ghosts[]) {
    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (shared->pacman_y == ghosts[i].y &&
            shared->pacman_x == ghosts[i].x) {

            printf("\n[COLISIÓN] Pac-Man chocó con fantasma %c\n",
                   ghosts[i].simbolo);

            printf("[COLISIÓN] Evento publicado por P2\n");
            printf("[COLISIÓN] Tick: %d\n", shared->global_tick);

            shared->collision_detected = 1;
            shared->collision_tick = shared->global_tick;
            shared->collision_ghost_id = ghosts[i].id;

            return 1;
        }
    }

    shared->collision_detected = 0;
    shared->collision_tick = -1;
    shared->collision_ghost_id = -1;

    return 0;
}

void imprimir_vidas(SharedData *shared) {
    printf("Vidas de Pac-Man: %d\n", shared->pacman_lives);
}

