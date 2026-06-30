#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <semaphore.h>

#define MAX_Y 50
#define MAX_X 50
#define NUM_GHOSTS 4
#define MAX_MOVE 64

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
        Error al abrir o interpretar un archivo de movimientos.
        input_error_process usa 1 para P1 y 2 para P2.
    */
    int input_error;
    int input_error_process;

    /*
        Punto 10: banderas de finalizacion por agotamiento de entradas.

        pacman_moves_finished:
            P1 lo activa cuando ya consumio todas sus instrucciones.

        ghost_moves_finished[i]:
            P2 activa la posicion i cuando ese fantasma agoto su archivo.

        P0 termina la simulacion cuando P1 y los cuatro fantasmas
        agotaron sus entradas.
    */
    int pacman_moves_finished;
    int ghost_moves_finished[NUM_GHOSTS];

    pthread_mutex_t mutex_shared;

    /*
        Semáforos del Checkpoint 7.

        sem_pacman_turn:
            P0 libera este semáforo cuando quiere que juegue P1.

        sem_enemy_turn:
            P0 libera este semáforo cuando quiere que juegue P2.

        sem_turn_done:
            P1 o P2 liberan este semáforo cuando terminaron su turno.
    */
    sem_t sem_pacman_turn;
    sem_t sem_enemy_turn;
    sem_t sem_turn_done;

} SharedData;

#endif

