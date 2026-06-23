#ifndef SYNC_UTILS_H
#define SYNC_UTILS_H

#include "shared_state.h"

/* Reserva memoria compartida anonima para shared_state_t. */
shared_state_t *shared_state_create(void);

/* Inicializa semaforos y mutex que viven dentro de shared_state_t. */
int shared_state_init_synchronization(shared_state_t *state);

/* Destruye semaforos y mutex antes de liberar la memoria compartida. */
void shared_state_destroy_synchronization(shared_state_t *state);

/* Libera la memoria compartida creada con mmap. */
void shared_state_release(shared_state_t *state);

#endif
