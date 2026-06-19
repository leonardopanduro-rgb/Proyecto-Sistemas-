#include "common.h"

const char *process_name(process_id_t id) {
    switch (id) {
        case PROCESS_SCHEDULER:
            return "P0 scheduler_process";
        case PROCESS_PACMAN:
            return "P1 pacman_process";
        case PROCESS_ENEMY:
            return "P2 enemy_process";
        default:
            return "unknown_process";
    }
}

const char *ghost_label(int ghost_id) {
    static const char *labels[NUM_GHOSTS] = {"A", "B", "C", "D"};

    if (ghost_id < 0 || ghost_id >= NUM_GHOSTS) {
        return "?";
    }

    return labels[ghost_id];
}

