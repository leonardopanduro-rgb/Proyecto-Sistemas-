#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p1_threads.h"
#include "pacman.h"

/*
 * Hilos internos de P1 y cola productor-consumidor.
 *
 * reader produce instrucciones; executor las consume solo cuando P0 publica
 * sem_pacman_turn; publisher confirma el turno con sem_turn_done.
 *
 * Protección contra race conditions:
 * - mutex_cola protege índices, cantidad, lector_termino y terminar.
 * - sem_hay_espacio impide sobrescribir una cola llena.
 * - sem_hay_movimientos impide consumir una cola vacía.
 * - sem_estado_pacman_listo ordena executor -> publisher.
 * - mutex_shared protege posición, score y buzones compartidos con P0/P2.
 */

/*
 * Inicializa el estado privado de los tres hilos de P1.
 * data vive hasta después de los pthread_join(), shared apunta al mmap común y
 * ruta_pacman identifica el archivo del reader. Los semáforos usan pshared=0
 * porque solo coordinan hilos de P1. Sus valores iniciales representan cero
 * elementos disponibles y P1_QUEUE_SIZE espacios libres.
 */
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

/* Destruye recursos privados de P1; se llama solo tras unir sus tres hilos. */
void destruir_pacman_thread_data(PacmanThreadData *data) {
    pthread_mutex_destroy(&data->mutex_cola);

    sem_destroy(&data->sem_hay_movimientos);
    sem_destroy(&data->sem_hay_espacio);
    sem_destroy(&data->sem_estado_pacman_listo);
}

/*
 * Lee terminar bajo mutex_cola. Esto evita la carrera entre el hilo principal,
 * que ordena el cierre, y reader/executor/publisher, que consultan la bandera.
 */
int pacman_debe_terminar(PacmanThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_cola);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_cola);

    return valor;
}

/* Publica terminar=1 bajo el mismo mutex usado por todas sus lecturas. */
void marcar_pacman_terminar(PacmanThreadData *data) {
    pthread_mutex_lock(&data->mutex_cola);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_cola);
}

/*
 * Inserta una instrucción en la cola circular. Primero reserva capacidad con
 * sem_hay_espacio; mutex_cola vuelve indivisible la actualización del arreglo,
 * final y cantidad; sem_hay_movimientos despierta después al consumidor.
 */
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

/*
 * Extrae una instrucción sin espera activa. mutex_cola evita carreras con el
 * reader al modificar frente/cantidad. Retorna 1 y copia el movimiento, o 0
 * si el reader llegó a EOF y la cola está vacía. En EOF repone el semáforo para
 * que futuros turnos tampoco queden bloqueados esperando datos inexistentes.
 */
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
 * Hilo 1 de P1: productor de movimientos.
 *
 * arg apunta a PacmanThreadData y retorna NULL, según pthread_create(). Lee y
 * valida pacman_moves.txt. Publica errores mediante una función sincronizada y
 * escribe lector_termino bajo mutex_cola. El sem_post final despierta al
 * executor para que distinga EOF de una cola temporalmente vacía.
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
 * Hilo 2 de P1: consumidor y ejecutor.
 *
 * sem_pacman_turn garantiza que P1 no actúe sin permiso de P0. game_over se
 * consulta con mutex_shared. SET_PRIORITY se deposita en un buzón para que P0
 * lo aplique; un movimiento normal se ejecuta bajo mutex_shared para que P2 y
 * el renderer no observen posición y score a medio actualizar.
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
                 * SECCIÓN CRÍTICA: mover_pacman modifica pacman_y, pacman_x
                 * y pacman_score. El mismo mutex_shared usado por sus lectores
                 * evita que P0, P2 o P3 vean una actualización parcial.
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
 * Hilo 3 de P1: publicación y confirmación del turno.
 *
 * sem_estado_pacman_listo garantiza que el executor acabó. Posición y score se
 * leen juntos bajo mutex_shared para formar una instantánea consistente. Solo
 * entonces publica sem_turn_done, por lo que P0 no inicia otro tick mientras
 * la acción de P1 siga incompleta.
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
