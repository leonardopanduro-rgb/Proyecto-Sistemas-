#include "pacman_process.h"

#include <stdio.h>

#include "common.h"

void pacman_process_bootstrap(const shared_state_t *state) {
    printf("[P1] %s listo\n", process_name(PROCESS_PACMAN));
    printf("[P1] Posicion inicial de Pac-Man: (%d,%d)\n",
           state->pacman_position.x,
           state->pacman_position.y);
}

