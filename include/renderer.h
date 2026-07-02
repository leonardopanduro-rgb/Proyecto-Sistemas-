#ifndef RENDERER_H
#define RENDERER_H

#include "shared.h"

/*
    BONUS: P3 = renderer_process.

    Cuarto proceso hijo creado por P0. Solo LEE la memoria compartida
    (map_grid, pacman, ghost_x/y, score, vidas y game_over) y dibuja el
    tablero en consola una vez por tick, sincronizado con P0 mediante
    sem_render_turn / sem_render_done.

    No modifica el estado del juego y solo toma mutex_shared el tiempo
    justo para copiar los datos, de modo que no ralentiza a P1/P2.

    Nunca retorna: al detectar game_over dibuja el cuadro final y llama
    exit(0), igual que pacman_process y enemy_process.
*/
void renderer_process(SharedData *shared);

#endif
