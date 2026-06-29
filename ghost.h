#ifndef GHOST_H
#define GHOST_H

#include "shared.h"

typedef struct {
    int id;
    char simbolo;
    int y;
    int x;
} GhostState;

void inicializar_fantasmas_desde_shared(SharedData *shared, GhostState ghosts[]);
void imprimir_fantasmas(GhostState ghosts[]);
void mover_fantasma(SharedData *shared, GhostState *ghost, const char *movimiento);
int detectar_colision(SharedData *shared, GhostState ghosts[]);

#endif

