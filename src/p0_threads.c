#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p0_threads.h"

void inicializar_scheduler_thread_data(SchedulerThreadData *data, SharedData *shared) {
    data->shared = shared;
    data->turno_actual = 0;

    /*
        ultimo_turno inicia en 2 para que el primer empate dé el turno a P1.
    */
    data->ultimo_turno = 2;
    data->terminar = 0;

    pthread_mutex_init(&data->mutex_scheduler, NULL);

    sem_init(&data->sem_tick_start, 0, 0);
    sem_init(&data->sem_tick_ready, 0, 0);
    sem_init(&data->sem_turn_ready, 0, 0);
    sem_init(&data->sem_tick_finished, 0, 0);
}

void destruir_scheduler_thread_data(SchedulerThreadData *data) {
    pthread_mutex_destroy(&data->mutex_scheduler);

    sem_destroy(&data->sem_tick_start);
    sem_destroy(&data->sem_tick_ready);
    sem_destroy(&data->sem_turn_ready);
    sem_destroy(&data->sem_tick_finished);
}

/*
    Punto 13 aplicado tambien a P0: terminar se accede solo con mutex.
*/
int scheduler_debe_terminar(SchedulerThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_scheduler);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_scheduler);

    return valor;
}

void marcar_scheduler_terminar(SchedulerThreadData *data) {
    pthread_mutex_lock(&data->mutex_scheduler);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_scheduler);
}

/*
    tick_thread:
    espera sem_tick_start, incrementa global_tick y publica sem_tick_ready.
*/
void *tick_thread(void *arg) {
    SchedulerThreadData *data = (SchedulerThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P0-tick] tick_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_tick_start);

        if (scheduler_debe_terminar(data)) {
            break;
        }

        pthread_mutex_lock(&shared->mutex_shared);
        shared->global_tick++;
        int tick_actual = shared->global_tick;
        pthread_mutex_unlock(&shared->mutex_shared);

        printf("\n[P0] ==============================\n");
        printf("[P0] Tick global %d\n", tick_actual);

        sem_post(&data->sem_tick_ready);
    }

    printf("[P0-tick] tick_thread finalizado\n");

    return NULL;
}

/*
    scheduler_thread:
    procesa solicitudes de prioridad, compara prioridades y escribe
    turno_actual.
*/
void *scheduler_thread(void *arg) {
    SchedulerThreadData *data = (SchedulerThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P0-scheduler] scheduler_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_tick_ready);

        if (scheduler_debe_terminar(data)) {
            break;
        }

        procesar_solicitudes_prioridad(shared);

        int turno = elegir_turno_por_prioridad(shared, &data->ultimo_turno);

        pthread_mutex_lock(&data->mutex_scheduler);
        data->turno_actual = turno;
        pthread_mutex_unlock(&data->mutex_scheduler);

        sem_post(&data->sem_turn_ready);
    }

    printf("[P0-scheduler] scheduler_thread finalizado\n");

    return NULL;
}

/*
    signal_thread:
    libera el semaforo del proceso elegido, espera sem_turn_done y confirma
    el final del tick con sem_tick_finished.
*/
void *signal_thread(void *arg) {
    SchedulerThreadData *data = (SchedulerThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P0-signal] signal_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_turn_ready);

        if (scheduler_debe_terminar(data)) {
            break;
        }

        pthread_mutex_lock(&data->mutex_scheduler);
        int turno = data->turno_actual;
        pthread_mutex_unlock(&data->mutex_scheduler);

        if (turno == 1) {
            printf("[P0] Turno elegido: P1\n");
            sem_post(&shared->sem_pacman_turn);
        } else {
            printf("[P0] Turno elegido: P2\n");
            sem_post(&shared->sem_enemy_turn);
        }

        sem_wait(&shared->sem_turn_done);

        printf("[P0] Fin de turno confirmado\n");

        sem_post(&data->sem_tick_finished);
    }

    printf("[P0-signal] signal_thread finalizado\n");

    return NULL;
}
