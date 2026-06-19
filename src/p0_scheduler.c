#include "p0_scheduler.h"

#include <stdio.h>
#include <string.h>

#include "p2_enemy_process.h"
#include "map.h"
#include "p1_pacman_process.h"

static void build_map_path(const char *case_dir, char *output, size_t output_size) {
    snprintf(output, output_size, "%s/%s", case_dir, "map.txt");
}

static void shared_state_init_from_map(shared_state_t *state, const game_map_t *map) {
    memset(state, 0, sizeof(*state));

    state->global_tick = 0;
    state->max_ticks = 100;
    state->game_over = 0;
    state->pacman_score = 0;
    state->pacman_lives = 3;
    state->prioridad_pacman = 20;
    state->prioridad_enemy = 30;

    state->map_rows = map->rows;
    state->map_cols = map->cols;
    state->pacman_start = map->pacman_start;
    state->pacman_position = map->pacman_start;

    for (int i = 0; i < NUM_GHOSTS; ++i) {
        state->ghost_start[i] = map->ghost_start[i];
    }

    for (int row = 0; row < map->rows; ++row) {
        memcpy(state->map_grid[row], map->grid[row], (size_t)map->cols + 1);
    }
}

int scheduler_process_main(const char *case_dir) {
    char map_path[MAX_PATH_LENGTH];
    char error[MAX_ERROR_LENGTH];
    game_map_t map;
    shared_state_t state;

    printf("[P0] %s inicializando arquitectura base\n",
           process_name(PROCESS_SCHEDULER));

    build_map_path(case_dir, map_path, sizeof(map_path));
    printf("[P0] Leyendo mapa: %s\n", map_path);

    if (map_load(map_path, &map, error, sizeof(error)) != 0) {
        fprintf(stderr, "[P0] Error al cargar mapa: %s\n", error);
        return 1;
    }

    map_print_summary(&map);
    shared_state_init_from_map(&state, &map);

    printf("[P0] Estado compartido base inicializado\n");
    printf("[P0] Prioridades iniciales: Pac-Man=%d, Enemigos=%d\n",
           state.prioridad_pacman,
           state.prioridad_enemy);

    pacman_process_bootstrap(&state);
    enemy_process_bootstrap(&state);

    printf("[P0] Avance base completado: arquitectura y mapa listos\n");
    return 0;
}
