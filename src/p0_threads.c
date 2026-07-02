#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p0_threads.h"

/*
    Inicializa la tuberia interna de P0:
    P0-main -> tick_thread -> scheduler_thread -> signal_thread -> P0-main.
    Los semaforos usan pshared=0 porque estos hilos viven en el mismo proceso.
    Inician en cero para impedir que una etapa adelante a la anterior.
*/
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

/* Destruye los recursos solo despues de hacer join de los tres hilos de P0. */
void destruir_scheduler_thread_data(SchedulerThreadData *data) {
    pthread_mutex_destroy(&data->mutex_scheduler);

    sem_destroy(&data->sem_tick_start);
    sem_destroy(&data->sem_tick_ready);
    sem_destroy(&data->sem_turn_ready);
    sem_destroy(&data->sem_tick_finished);
}

/*
    Lee terminar bajo mutex_scheduler. Esto evita una carrera entre P0-main,
    que solicita el cierre, y los tres hilos que consultan la bandera.
*/
int scheduler_debe_terminar(SchedulerThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_scheduler);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_scheduler);

    return valor;
}

/* Activa terminar bajo el mismo mutex usado por scheduler_debe_terminar(). */
void marcar_scheduler_terminar(SchedulerThreadData *data) {
    pthread_mutex_lock(&data->mutex_scheduler);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_scheduler);
}

/*
    Primera etapa del tick.
    sem_tick_start impide incrementar por cuenta propia. global_tick se modifica
    bajo mutex_shared para sincronizarlo con P0/P1/P2/P3. sem_tick_ready entrega
    exactamente un tick al scheduler_thread.
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
    Segunda etapa del tick.
    Espera sem_tick_ready, consume SET_PRIORITY y calcula turno_actual. El campo
    local se protege con mutex_scheduler porque signal_thread lo lee en paralelo.
    sem_turn_ready garantiza que signal_thread no use un turno antiguo.
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
    Tercera etapa del tick.
    Publica solo sem_pacman_turn o sem_enemy_turn: nunca ambos. Luego espera
    sem_turn_done, que actua como barrera entre procesos. Sin esta espera P0
    podria incrementar otro tick mientras P1/P2 aun modifica el estado.
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
