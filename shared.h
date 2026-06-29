#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>

#define MAX_Y 50
#define MAX_X 50
#define NUM_GHOSTS 4
#define MAX_MOVE 64

/*
    SharedData representa el estado central del juego.

    En Checkpoint 5 todavía NO usamos procesos ni hilos.
    Pero ya dejamos todos los datos importantes dentro de esta
    estructura para que luego P0, P1 y P2 puedan compartirlos.
*/
typedef struct {
    int global_tick;
    int max_ticks;
    int game_over;

    int filas;
    int columnas;

    char map_grid[MAX_Y][MAX_X];

    int pacman_x;
    int pacman_y;
    int pacman_score;
    int pacman_lives;

    int ghost_start_x[NUM_GHOSTS];
    int ghost_start_y[NUM_GHOSTS];

    int collision_detected;
    int collision_tick;
    int collision_ghost_id;

    int prioridad_pacman;
    int prioridad_enemy;

    int pending_priority_pacman;
    int priority_request_active;

    int pending_priority_enemy;
    int enemy_priority_request_active;

    /*
        Este mutex todavía no se usa fuerte en Checkpoint 5.
        Lo dejamos preparado para Checkpoint 6 en adelante.
    */
    pthread_mutex_t mutex_shared;

} SharedData;

#endif

