#ifndef P1_PACMAN_PROCESS_H
#define P1_PACMAN_PROCESS_H

#include "shared_state.h"

void pacman_process_bootstrap(const shared_state_t *state);
int pacman_process_main(shared_state_t *state, const char *case_dir);

#endif
