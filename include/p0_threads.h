#ifndef P0_THREADS_H
#define P0_THREADS_H

#include <pthread.h>
#include <semaphore.h>

#include "shared.h"

typedef struct {
    SharedData *shared;
    int turno_actual;
    int ultimo_turno;
    int terminar;
    pthread_mutex_t mutex_scheduler;
    sem_t sem_tick_start;
    sem_t sem_tick_ready;
    sem_t sem_turn_ready;
    sem_t sem_tick_finished;
} SchedulerThreadData;

void inicializar_scheduler_thread_data(SchedulerThreadData *data,
                                       SharedData *shared);
void destruir_scheduler_thread_data(SchedulerThreadData *data);
int scheduler_debe_terminar(SchedulerThreadData *data);
void marcar_scheduler_terminar(SchedulerThreadData *data);
void *tick_thread(void *arg);
void *scheduler_thread(void *arg);
void *signal_thread(void *arg);

#endif
