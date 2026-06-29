#ifndef COLLISION_H
#define COLLISION_H

#include "shared.h"
#include "ghost.h"

/*
    Detecta si Pac-Man chocó con algún fantasma.

    IMPORTANTE:
    Esta función NO baja vidas.
    Solo publica el evento de colisión en memoria compartida.
*/
int verificar_colision(SharedData *shared, GhostState ghosts[]);

void imprimir_vidas(SharedData *shared);

#endif

