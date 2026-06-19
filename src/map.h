#ifndef MAP_H
#define MAP_H

#include <stddef.h>

#include "common.h"

typedef struct {
    int rows;
    int cols;
    char grid[MAX_MAP_ROWS][MAX_MAP_COLS + 1];

    int found_pacman;
    int found_ghost[NUM_GHOSTS];
    position_t pacman_start;
    position_t ghost_start[NUM_GHOSTS];
} game_map_t;

void map_init(game_map_t *map);
int map_load(const char *path, game_map_t *map, char *error, size_t error_size);
int map_is_walkable(char cell);
void map_print_summary(const game_map_t *map);

#endif

