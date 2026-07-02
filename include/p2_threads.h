#ifndef P2_THREADS_H
#define P2_THREADS_H

#include <pthread.h>
#include <semaphore.h>

#include "ghost.h"
#include "shared.h"

typedef struct {
    SharedData *shared;
    GhostState ghosts[NUM_GHOSTS];
    char rutas_ghost[NUM_GHOSTS][256];
    int pacman_last_y;
    int pacman_last_x;
    int pacman_previous_y;
    int pacman_previous_x;
    int ghost_previous_y[NUM_GHOSTS];
    int ghost_previous_x[NUM_GHOSTS];
    int terminar;
    pthread_mutex_t mutex_ghosts;
    pthread_mutex_t mutex_pacman_local;
    pthread_mutex_t mutex_terminar;
    sem_t sem_ghost_turn[NUM_GHOSTS];
    sem_t sem_ghost_done[NUM_GHOSTS];
    sem_t sem_tracker_start;
    sem_t sem_tracker_done;
    sem_t sem_collision_start;
    sem_t sem_collision_done;
} EnemyThreadData;

typedef struct {
    EnemyThreadData *data;
    int ghost_index;
} GhostThreadArg;

void inicializar_enemy_thread_data(EnemyThreadData *data,
                                   SharedData *shared,
                                   const char *carpeta_caso);
void destruir_enemy_thread_data(EnemyThreadData *data);
int enemy_debe_terminar(EnemyThreadData *data);
void marcar_enemy_terminar(EnemyThreadData *data);
void *enemy_controller(void *arg);
void *ghost_thread_1(void *arg);
void *ghost_thread_2(void *arg);
void *ghost_thread_3(void *arg);
void *ghost_thread_4(void *arg);
void *pacman_tracker_thread(void *arg);
void *collision_thread(void *arg);

#endif
