#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"

SharedData *crear_memoria_compartida() {
    SharedData *shared = mmap(
        NULL,
        sizeof(SharedData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (shared == MAP_FAILED) {
        perror("Error al crear memoria compartida con mmap");
        exit(1);
    }

    return shared;
}

void inicializar_shared(SharedData *shared) {
    shared->global_tick = 0;
    shared->max_ticks = 10;
    shared->game_over = 0;

    shared->filas = 0;
    shared->columnas = 0;

    for (int y = 0; y < MAX_Y; y++) {
        for (int x = 0; x < MAX_X; x++) {
            shared->map_grid[y][x] = '\0';
        }
    }

    shared->pacman_x = 0;
    shared->pacman_y = 0;
    shared->pacman_score = 0;
    shared->pacman_lives = 3;

    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_start_x[i] = 0;
        shared->ghost_start_y[i] = 0;
    }

    /*
        BONUS P3: posiciones actuales de los fantasmas.
        Se ponen en las de inicio despues de cargar el mapa.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_x[i] = 0;
        shared->ghost_y[i] = 0;
    }

    shared->collision_detected = 0;
    shared->collision_tick = -1;
    shared->collision_ghost_id = -1;

    shared->prioridad_pacman = 30;
    shared->prioridad_enemy = 30;

    shared->pending_priority_pacman = 0;
    shared->priority_request_active = 0;

    shared->pending_priority_enemy = 0;
    shared->enemy_priority_request_active = 0;

    shared->input_error = 0;
    shared->input_error_process = 0;

    shared->pacman_moves_finished = 0;
    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_moves_finished[i] = 0;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared->mutex_shared, &attr);
    pthread_mutexattr_destroy(&attr);

    sem_init(&shared->sem_pacman_turn, 1, 0);
    sem_init(&shared->sem_enemy_turn, 1, 0);
    sem_init(&shared->sem_turn_done, 1, 0);

    /*
        BONUS P3: semaforos process-shared para sincronizar el renderer.
    */
    sem_init(&shared->sem_render_turn, 1, 0);
    sem_init(&shared->sem_render_done, 1, 0);
}

void liberar_memoria_compartida(SharedData *shared) {
    sem_destroy(&shared->sem_pacman_turn);
    sem_destroy(&shared->sem_enemy_turn);
    sem_destroy(&shared->sem_turn_done);

    sem_destroy(&shared->sem_render_turn);
    sem_destroy(&shared->sem_render_done);

    pthread_mutex_destroy(&shared->mutex_shared);

    if (munmap(shared, sizeof(SharedData)) == -1) {
        perror("Error al liberar memoria compartida");
    }
}

void construir_ruta(char destino[], int tam, const char *carpeta_caso, const char *archivo) {
    snprintf(destino, tam, "%s/%s", carpeta_caso, archivo);
}

int obtener_game_over(SharedData *shared) {
    int game_over;

    pthread_mutex_lock(&shared->mutex_shared);
    game_over = shared->game_over;
    pthread_mutex_unlock(&shared->mutex_shared);

    return game_over;
}

void establecer_game_over(SharedData *shared) {
    pthread_mutex_lock(&shared->mutex_shared);
    shared->game_over = 1;
    pthread_mutex_unlock(&shared->mutex_shared);
}

int obtener_global_tick(SharedData *shared) {
    int tick;

    pthread_mutex_lock(&shared->mutex_shared);
    tick = shared->global_tick;
    pthread_mutex_unlock(&shared->mutex_shared);

    return tick;
}

void obtener_control_juego(
    SharedData *shared,
    int *game_over,
    int *global_tick,
    int *max_ticks,
    int *pacman_lives
) {
    pthread_mutex_lock(&shared->mutex_shared);

    *game_over = shared->game_over;
    *global_tick = shared->global_tick;
    *max_ticks = shared->max_ticks;
    *pacman_lives = shared->pacman_lives;

    pthread_mutex_unlock(&shared->mutex_shared);
}
