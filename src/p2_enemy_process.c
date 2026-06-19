#include "p2_enemy_process.h"

#include <stdio.h>

#include "common.h"

void enemy_process_bootstrap(const shared_state_t *state) {
    printf("[P2] %s listo para controlar %d fantasmas\n",
           process_name(PROCESS_ENEMY),
           NUM_GHOSTS);

    for (int i = 0; i < NUM_GHOSTS; ++i) {
        printf("[P2] Fantasma %s inicia en (%d,%d)\n",
               ghost_label(i),
               state->ghost_start[i].x,
               state->ghost_start[i].y);
    }
}
