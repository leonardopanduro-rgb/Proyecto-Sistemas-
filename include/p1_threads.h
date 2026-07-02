#ifndef P1_THREADS_H
#define P1_THREADS_H

#include <pthread.h>
#include <semaphore.h>

#include "shared.h"

#define P1_QUEUE_SIZE 128

typedef struct {
    char movimientos[P1_QUEUE_SIZE][MAX_MOVE];
    int frente;
    int final;
    int cantidad;
    int lector_termino;
    int terminar;
    pthread_mutex_t mutex_cola;
    sem_t sem_hay_movimientos;
    sem_t sem_hay_espacio;
    sem_t sem_estado_pacman_listo;
    SharedData *shared;
    char ruta_pacman[256];
} PacmanThreadData;

void inicializar_pacman_thread_data(PacmanThreadData *data,
                                    SharedData *shared,
                                    const char *ruta_pacman);
void destruir_pacman_thread_data(PacmanThreadData *data);
int pacman_debe_terminar(PacmanThreadData *data);
void marcar_pacman_terminar(PacmanThreadData *data);
void *movement_reader_thread(void *arg);
void *movement_executor_thread(void *arg);
void *pacman_publisher_thread(void *arg);

#endif
