#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "common.h"

typedef struct {
    int global_tick;
    int max_ticks;
    int game_over;

    int map_rows;
    int map_cols;
    char map_grid[MAX_MAP_ROWS][MAX_MAP_COLS + 1];

    position_t pacman_start;
    position_t pacman_position;
    int pacman_score;
    int pacman_lives;

    position_t ghost_start[NUM_GHOSTS];

    int collision_detected;
    int collision_tick;
    int collision_ghost_id;

    int prioridad_pacman;
    int prioridad_enemy;

    int pending_priority_pacman;
    int priority_request_active;
    int pending_priority_enemy;
    int enemy_priority_request_active;
} shared_state_t;

#endif

