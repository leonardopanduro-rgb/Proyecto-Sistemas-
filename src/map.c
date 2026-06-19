#include "map.h"

#include <stdio.h>
#include <string.h>

static int is_allowed_cell(char cell) {
    return cell == 'X' ||
           cell == 'O' ||
           cell == '*' ||
           cell == 'P' ||
           cell == 'A' ||
           cell == 'B' ||
           cell == 'C' ||
           cell == 'D';
}

static int ghost_index_from_cell(char cell) {
    if (cell >= 'A' && cell <= 'D') {
        return cell - 'A';
    }

    return -1;
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static void set_position_error(char *error,
                               size_t error_size,
                               const char *prefix,
                               int row,
                               int col) {
    if (error == NULL || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s en fila %d, columna %d", prefix, row, col);
}

void map_init(game_map_t *map) {
    memset(map, 0, sizeof(*map));

    map->pacman_start.x = -1;
    map->pacman_start.y = -1;

    for (int i = 0; i < NUM_GHOSTS; ++i) {
        map->ghost_start[i].x = -1;
        map->ghost_start[i].y = -1;
    }
}

int map_load(const char *path, game_map_t *map, char *error, size_t error_size) {
    FILE *file = fopen(path, "r");
    char line[MAX_MAP_COLS + 4];
    int expected_cols = -1;

    if (file == NULL) {
        set_error(error, error_size, "No se pudo abrir map.txt");
        return -1;
    }

    map_init(map);

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';

        if (len == 0) {
            continue;
        }

        if (len > MAX_MAP_COLS) {
            fclose(file);
            set_position_error(error,
                               error_size,
                               "Linea excede MAX_MAP_COLS",
                               map->rows + 1,
                               MAX_MAP_COLS);
            return -1;
        }

        if (map->rows >= MAX_MAP_ROWS) {
            fclose(file);
            set_error(error, error_size, "El mapa excede MAX_MAP_ROWS");
            return -1;
        }

        if (expected_cols == -1) {
            expected_cols = (int)len;
        } else if ((int)len != expected_cols) {
            fclose(file);
            set_position_error(error,
                               error_size,
                               "Todas las filas deben tener el mismo ancho",
                               map->rows + 1,
                               (int)len);
            return -1;
        }

        for (int col = 0; col < (int)len; ++col) {
            char cell = line[col];

            if (!is_allowed_cell(cell)) {
                fclose(file);
                set_position_error(error,
                                   error_size,
                                   "Simbolo invalido en el mapa",
                                   map->rows + 1,
                                   col + 1);
                return -1;
            }

            if (cell == 'P') {
                if (map->found_pacman) {
                    fclose(file);
                    set_position_error(error,
                                       error_size,
                                       "Pac-Man aparece mas de una vez",
                                       map->rows + 1,
                                       col + 1);
                    return -1;
                }

                map->found_pacman = 1;
                map->pacman_start.x = col;
                map->pacman_start.y = map->rows;
            }

            int ghost_id = ghost_index_from_cell(cell);
            if (ghost_id >= 0) {
                if (map->found_ghost[ghost_id]) {
                    fclose(file);
                    set_position_error(error,
                                       error_size,
                                       "Un fantasma aparece mas de una vez",
                                       map->rows + 1,
                                       col + 1);
                    return -1;
                }

                map->found_ghost[ghost_id] = 1;
                map->ghost_start[ghost_id].x = col;
                map->ghost_start[ghost_id].y = map->rows;
            }
        }

        memcpy(map->grid[map->rows], line, len + 1);
        map->rows++;
    }

    fclose(file);

    if (map->rows == 0) {
        set_error(error, error_size, "map.txt esta vacio");
        return -1;
    }

    map->cols = expected_cols;

    if (!map->found_pacman) {
        set_error(error, error_size, "El mapa no contiene posicion inicial P");
        return -1;
    }

    for (int i = 0; i < NUM_GHOSTS; ++i) {
        if (!map->found_ghost[i]) {
            snprintf(error,
                     error_size,
                     "El mapa no contiene posicion inicial del fantasma %s",
                     ghost_label(i));
            return -1;
        }
    }

    return 0;
}

int map_is_walkable(char cell) {
    return cell == 'O' ||
           cell == '*' ||
           cell == 'P' ||
           cell == 'A' ||
           cell == 'B' ||
           cell == 'C' ||
           cell == 'D';
}

void map_print_summary(const game_map_t *map) {
    printf("Mapa cargado: %d filas x %d columnas\n", map->rows, map->cols);
    printf("Pac-Man inicia en (%d,%d)\n",
           map->pacman_start.x,
           map->pacman_start.y);

    for (int i = 0; i < NUM_GHOSTS; ++i) {
        printf("Fantasma %s inicia en (%d,%d)\n",
               ghost_label(i),
               map->ghost_start[i].x,
               map->ghost_start[i].y);
    }
}
