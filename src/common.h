#ifndef COMMON_H
#define COMMON_H

#define MAX_MAP_ROWS 64
#define MAX_MAP_COLS 128
#define NUM_GHOSTS 4
#define MAX_ERROR_LENGTH 256
#define MAX_PATH_LENGTH 512

typedef struct {
    int x;
    int y;
} position_t;

typedef enum {
    PROCESS_SCHEDULER = 0,
    PROCESS_PACMAN = 1,
    PROCESS_ENEMY = 2
} process_id_t;

const char *process_name(process_id_t id);
const char *ghost_label(int ghost_id);

#endif

