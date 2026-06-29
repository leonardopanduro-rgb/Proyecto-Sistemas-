#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <pthread.h>
#include <semaphore.h>

#include "common.h"

typedef struct {
    /* Semaforo que P0 libera cuando le toca jugar a Pac-Man. */
    sem_t sem_pacman_turn;

    /* Semaforo que P0 libera cuando les toca jugar a los fantasmas. */
    sem_t sem_enemy_turn;

    /* Semaforo que P1 o P2 liberan para avisar que terminaron su turno. */
    sem_t sem_turn_done;

    /* Mutex para proteger tick, game_over, mapa, posiciones y contadores. */
    pthread_mutex_t state_mutex;

    /* Mutex para proteger las prioridades del scheduler. */
    pthread_mutex_t priority_mutex;

    /* Mutex reservado para proteger eventos de colision. */
    pthread_mutex_t collision_mutex;

    /* Tick global controlado por P0. */
    int global_tick;

    /* Cantidad maxima de ticks para este avance demostrativo. */
    int max_ticks;

    /* Bandera que P0 activa para pedir cierre ordenado. */
    int game_over;

    /* Cantidad de filas del mapa. */
    int map_rows;

    /* Cantidad de columnas del mapa. */
    int map_cols;

    /* Copia del mapa para que los procesos puedan consultarlo. */
    char map_grid[MAX_MAP_ROWS][MAX_MAP_COLS + 1];

    /* Posicion inicial de Pac-Man. */
    position_t pacman_start;

    /* Posicion actual de Pac-Man. */
    position_t pacman_position;

    /* Puntaje acumulado de Pac-Man. */
    int pacman_score;

    /* Vidas restantes de Pac-Man. */
    int pacman_lives;

    /* Ronda actual (aumenta tras cada colisión) para reiniciar movimientos. */
    int current_round;
    
    /* Cantidad de turnos que P1 ejecuto. */
    int pacman_turns_executed;

    /* Cantidad de instrucciones leidas desde pacman_moves.txt. */
    int pacman_moves_loaded;

    /* Posiciones iniciales de los cuatro fantasmas. */
    position_t ghost_start[NUM_GHOSTS];

    /* Posiciones actuales de los cuatro fantasmas. (¡NUEVO!) */
    position_t ghost_position[NUM_GHOSTS];

    /* Cantidad de turnos que P2 ejecuto. */
    int ghost_turns_executed;

    /* Cantidad de instrucciones leidas por cada fantasma. */
    int ghost_moves_loaded[NUM_GHOSTS];

    /* Bandera que P2 usara para avisar una colision. */
    int collision_detected;

    /* Tick en el que ocurrio la colision. */
    int collision_tick;

    /* Identificador del fantasma que colisiono. */
    int collision_ghost_id;

    /* Prioridad actual del proceso P1. */
    int prioridad_pacman;

    /* Prioridad actual del proceso P2. */
    int prioridad_enemy;

    /* Nueva prioridad solicitada por P1. */
    int pending_priority_pacman;

    /* Bandera que indica si P1 pidio cambio de prioridad. */
    int priority_request_active;

    /* Nueva prioridad solicitada por P2. */
    int pending_priority_enemy;

    /* Bandera que indica si P2 pidio cambio de prioridad. */
    int enemy_priority_request_active;
} shared_state_t;

#endif
