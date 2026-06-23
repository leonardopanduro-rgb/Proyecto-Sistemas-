#ifndef P2_ENEMY_PROCESS_H
#define P2_ENEMY_PROCESS_H

#include "shared_state.h"

void enemy_process_bootstrap(const shared_state_t *state);
int enemy_process_main(shared_state_t *state, const char *case_dir);

#endif
