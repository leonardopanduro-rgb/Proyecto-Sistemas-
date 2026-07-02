#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p1_threads.h"
#include "pacman.h"

void inicializar_pacman_thread_data(
    PacmanThreadData *data,
    SharedData *shared,
    const char *ruta_pacman
) {
    data->frente = 0;
    data->final = 0;
    data->cantidad = 0;

    data->lector_termino = 0;
    data->terminar = 0;

    data->shared = shared;

    strncpy(data->ruta_pacman, ruta_pacman, sizeof(data->ruta_pacman) - 1);
    data->ruta_pacman[sizeof(data->ruta_pacman) - 1] = '\0';

    pthread_mutex_init(&data->mutex_cola, NULL);

    sem_init(&data->sem_hay_movimientos, 0, 0);
    sem_init(&data->sem_hay_espacio, 0, P1_QUEUE_SIZE);
    sem_init(&data->sem_estado_pacman_listo, 0, 0);
}

void destruir_pacman_thread_data(PacmanThreadData *data) {
    pthread_mutex_destroy(&data->mutex_cola);

    sem_destroy(&data->sem_hay_movimientos);
    sem_destroy(&data->sem_hay_espacio);
    sem_destroy(&data->sem_estado_pacman_listo);
}

/*
    Punto 13.
    El acceso a data->terminar se centraliza bajo mutex_cola para que
    ningun hilo de P1 lea o escriba la bandera directamente.
*/
int pacman_debe_terminar(PacmanThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_cola);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_cola);

    return valor;
}

void marcar_pacman_terminar(PacmanThreadData *data) {
    pthread_mutex_lock(&data->mutex_cola);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_cola);
}

void cola_insertar_movimiento(PacmanThreadData *data, const char *movimiento) {
    sem_wait(&data->sem_hay_espacio);

    pthread_mutex_lock(&data->mutex_cola);

    if (data->terminar == 0) {
        strncpy(data->movimientos[data->final], movimiento, MAX_MOVE - 1);
        data->movimientos[data->final][MAX_MOVE - 1] = '\0';

        data->final = (data->final + 1) % P1_QUEUE_SIZE;
        data->cantidad++;

        printf("[P1-reader] Movimiento insertado en cola: %s\n",
               movimiento);
    }

    pthread_mutex_unlock(&data->mutex_cola);

    sem_post(&data->sem_hay_movimientos);
}

int cola_sacar_movimiento(PacmanThreadData *data, char movimiento[]) {
    while (1) {
        sem_wait(&data->sem_hay_movimientos);

        pthread_mutex_lock(&data->mutex_cola);

        if (data->cantidad > 0) {
            strncpy(movimiento, data->movimientos[data->frente], MAX_MOVE - 1);
            movimiento[MAX_MOVE - 1] = '\0';

            data->frente = (data->frente + 1) % P1_QUEUE_SIZE;
            data->cantidad--;

            pthread_mutex_unlock(&data->mutex_cola);

            sem_post(&data->sem_hay_espacio);

            return 1;
        }

        if (data->lector_termino == 1) {
            pthread_mutex_unlock(&data->mutex_cola);

            /*
                Dejamos el semáforo disponible para que futuros turnos
                no se queden bloqueados esperando movimientos inexistentes.
            */
            sem_post(&data->sem_hay_movimientos);

            return 0;
        }

        pthread_mutex_unlock(&data->mutex_cola);
    }
}

/*
    Hilo 1 de P1:
    movement_reader_thread

    Lee pacman_moves.txt y mete las instrucciones en una cola interna.
*/
void *movement_reader_thread(void *arg) {
    PacmanThreadData *data = (PacmanThreadData *)arg;

    printf("[P1-reader] movement_reader_thread iniciado\n");

    FILE *archivo_pacman = fopen(data->ruta_pacman, "r");

    if (archivo_pacman == NULL) {
        printf("[P1-reader] No se pudo abrir %s\n", data->ruta_pacman);

        publicar_error_entrada(data->shared, 1);

        pthread_mutex_lock(&data->mutex_cola);
        data->lector_termino = 1;
        pthread_mutex_unlock(&data->mutex_cola);

        sem_post(&data->sem_hay_movimientos);

        return NULL;
    }

    char movimiento[MAX_MOVE];

    while (pacman_debe_terminar(data) == 0) {
        int estado_lectura = leer_movimiento(archivo_pacman,
                                             movimiento,
                                             sizeof(movimiento));

        if (estado_lectura == LECTURA_FIN) {
            break;
        }

        if (estado_lectura == LECTURA_INVALIDA ||
            estado_lectura == LECTURA_ERROR) {
            publicar_error_entrada(data->shared, 1);
            break;
        }

        cola_insertar_movimiento(data, movimiento);
    }

    fclose(archivo_pacman);

    pthread_mutex_lock(&data->mutex_cola);
    data->lector_termino = 1;
    pthread_mutex_unlock(&data->mutex_cola);

    sem_post(&data->sem_hay_movimientos);

    printf("[P1-reader] movement_reader_thread finalizado\n");

    return NULL;
}

/*
    Hilo 2 de P1:
    movement_executor_thread

    Espera el turno que P0 le da a P1.
    Cuando recibe turno, consume una instrucción de la cola.
*/

void *movement_executor_thread(void *arg) {
    PacmanThreadData *data = (PacmanThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P1-executor] movement_executor_thread iniciado\n");

    while (1) {
        sem_wait(&shared->sem_pacman_turn);

        if (obtener_game_over(shared)) {
            marcar_pacman_terminar(data);
            sem_post(&data->sem_estado_pacman_listo);
            break;
        }

        printf("[P1-executor] Turno recibido en tick %d\n",
               obtener_global_tick(shared));

        char movimiento[MAX_MOVE];

        if (cola_sacar_movimiento(data, movimiento)) {
            int nueva_prioridad;

            if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                solicitar_prioridad_pacman(shared, nueva_prioridad);
            } else {
                /*
                    CHECKPOINT 13:
                    Protegemos la actualización de Pac-Man.

                    mover_pacman modifica:
                    - shared->pacman_y
                    - shared->pacman_x
                    - shared->pacman_score

                    Entonces debe ejecutarse dentro de mutex_shared.
                */
                pthread_mutex_lock(&shared->mutex_shared);

                mover_pacman(shared, movimiento);

                pthread_mutex_unlock(&shared->mutex_shared);
            }
        } else {
            printf("[P1-executor] No hay más movimientos de Pac-Man\n");
            publicar_pacman_agotado(shared);
        }

        sem_post(&data->sem_estado_pacman_listo);
    }

    printf("[P1-executor] movement_executor_thread finalizado\n");

    return NULL;
}




/*
    Hilo 3 de P1:
    pacman_publisher_thread

    Publica el estado de Pac-Man y avisa a P0 que P1 terminó su turno.
*/
void *pacman_publisher_thread(void *arg) {
    PacmanThreadData *data = (PacmanThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P1-publisher] pacman_publisher_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_estado_pacman_listo);

        if (pacman_debe_terminar(data) || obtener_game_over(shared)) {
            break;
        }

        pthread_mutex_lock(&shared->mutex_shared);

        printf("[P1-publisher] Estado publicado: Pac-Man=(%d,%d), score=%d\n",
               shared->pacman_y,
               shared->pacman_x,
               shared->pacman_score);

        pthread_mutex_unlock(&shared->mutex_shared);

        printf("[P1] Fin de turno\n");

        sem_post(&shared->sem_turn_done);
    }

    printf("[P1-publisher] pacman_publisher_thread finalizado\n");

    return NULL;
}
