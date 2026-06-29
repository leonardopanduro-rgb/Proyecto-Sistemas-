#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "shared.h"
#include "map.h"
#include "pacman.h"
#include "ghost.h"
#include "collision.h"

#define PRIORIDAD_MIN 1
#define PRIORIDAD_MAX 100

#define P1_QUEUE_SIZE 128

/*
    CHECKPOINT 11

    Objetivo:
    - Mantener P0 como scheduler.
    - Mantener P2 como proceso de enemigos.
    - Convertir P1 en un proceso con hilos internos.

    Hilos de P1:
    - movement_reader_thread
    - movement_executor_thread
    - pacman_publisher_thread

    Todavía NO implementamos:
    - hilos internos de P2
    - mutex fuertes para ghost_positions[]
    - cola avanzada de solicitudes de prioridad
*/

/*
    Cola interna de movimientos de Pac-Man.

    Esta cola pertenece solo al proceso P1.
    No está en memoria compartida porque P0 y P2 no necesitan verla.
*/
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

/*
    Datos internos del proceso P2.

    Esta estructura vive solo dentro de enemy_process.
    Sirve para coordinar los hilos internos de los fantasmas.
*/
typedef struct {
    SharedData *shared;

    GhostState ghosts[NUM_GHOSTS];

    char rutas_ghost[NUM_GHOSTS][256];

    int pacman_last_y;
    int pacman_last_x;

    int terminar;

    pthread_mutex_t mutex_ghosts;
    pthread_mutex_t mutex_pacman_local;

    sem_t sem_ghost_turn[NUM_GHOSTS];
    sem_t sem_ghost_done[NUM_GHOSTS];

    sem_t sem_tracker_start;
    sem_t sem_tracker_done;

    sem_t sem_collision_start;
    sem_t sem_collision_done;
} EnemyThreadData;

/*
    Argumento para cada ghost_thread.
*/
typedef struct {
    EnemyThreadData *data;
    int ghost_index;
} GhostThreadArg;




SharedData *crear_memoria_compartida() {
    SharedData *shared = mmap(
        NULL,
        sizeof(SharedData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (shared == MAP_FAILED) {
        perror("Error al crear memoria compartida con mmap");
        exit(1);
    }

    return shared;
}

void inicializar_shared(SharedData *shared) {
    shared->global_tick = 0;
    shared->max_ticks = 10;
    shared->game_over = 0;

    shared->filas = 0;
    shared->columnas = 0;

    for (int y = 0; y < MAX_Y; y++) {
        for (int x = 0; x < MAX_X; x++) {
            shared->map_grid[y][x] = '\0';
        }
    }

    shared->pacman_x = 0;
    shared->pacman_y = 0;
    shared->pacman_score = 0;
    shared->pacman_lives = 3;

    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_start_x[i] = 0;
        shared->ghost_start_y[i] = 0;
    }

    shared->collision_detected = 0;
    shared->collision_tick = -1;
    shared->collision_ghost_id = -1;

    shared->prioridad_pacman = 30;
    shared->prioridad_enemy = 30;

    shared->pending_priority_pacman = 0;
    shared->priority_request_active = 0;

    shared->pending_priority_enemy = 0;
    shared->enemy_priority_request_active = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared->mutex_shared, &attr);
    pthread_mutexattr_destroy(&attr);

    sem_init(&shared->sem_pacman_turn, 1, 0);
    sem_init(&shared->sem_enemy_turn, 1, 0);
    sem_init(&shared->sem_turn_done, 1, 0);
}

void liberar_memoria_compartida(SharedData *shared) {
    sem_destroy(&shared->sem_pacman_turn);
    sem_destroy(&shared->sem_enemy_turn);
    sem_destroy(&shared->sem_turn_done);

    pthread_mutex_destroy(&shared->mutex_shared);

    if (munmap(shared, sizeof(SharedData)) == -1) {
        perror("Error al liberar memoria compartida");
    }
}

void construir_ruta(char destino[], int tam, const char *carpeta_caso, const char *archivo) {
    snprintf(destino, tam, "%s/%s", carpeta_caso, archivo);
}

int leer_movimiento(FILE *archivo, char movimiento[], int tam) {
    if (archivo == NULL) {
        return 0;
    }

    if (fgets(movimiento, tam, archivo) == NULL) {
        return 0;
    }

    int largo = strlen(movimiento);

    if (largo > 0 && movimiento[largo - 1] == '\n') {
        movimiento[largo - 1] = '\0';
        largo--;
    }

    if (largo > 0 && movimiento[largo - 1] == '\r') {
        movimiento[largo - 1] = '\0';
        largo--;
    }

    if (strlen(movimiento) == 0) {
        return 0;
    }

    return 1;
}

int extraer_prioridad(const char *movimiento, int *nueva_prioridad) {
    char comando[32];
    int valor;

    if (sscanf(movimiento, "%31s %d", comando, &valor) == 2) {
        if (strcmp(comando, "SET_PRIORITY") == 0) {
            *nueva_prioridad = valor;
            return 1;
        }
    }

    return 0;
}

int prioridad_valida(int prioridad) {
    return prioridad >= PRIORIDAD_MIN && prioridad <= PRIORIDAD_MAX;
}

void solicitar_prioridad_pacman(SharedData *shared, int nueva_prioridad) {
    pthread_mutex_lock(&shared->mutex_shared);

    shared->pending_priority_pacman = nueva_prioridad;
    shared->priority_request_active = 1;

    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P1] Solicitud enviada a P0: cambiar prioridad Pac-Man a %d\n",
           nueva_prioridad);
}

void solicitar_prioridad_enemy(SharedData *shared, int nueva_prioridad) {
    pthread_mutex_lock(&shared->mutex_shared);

    shared->pending_priority_enemy = nueva_prioridad;
    shared->enemy_priority_request_active = 1;

    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P2] Solicitud enviada a P0: cambiar prioridad Enemigos a %d\n",
           nueva_prioridad);
}

void procesar_solicitudes_prioridad(SharedData *shared) {
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->priority_request_active == 1) {
        int nueva = shared->pending_priority_pacman;

        printf("[P0] Solicitud pendiente de P1: prioridad Pac-Man = %d\n",
               nueva);

        if (prioridad_valida(nueva)) {
            shared->prioridad_pacman = nueva;
            printf("[P0] Prioridad de Pac-Man actualizada oficialmente a %d\n",
                   shared->prioridad_pacman);
        } else {
            printf("[P0] Solicitud rechazada: prioridad Pac-Man fuera de rango\n");
        }

        shared->pending_priority_pacman = 0;
        shared->priority_request_active = 0;
    }

    if (shared->enemy_priority_request_active == 1) {
        int nueva = shared->pending_priority_enemy;

        printf("[P0] Solicitud pendiente de P2: prioridad Enemigos = %d\n",
               nueva);

        if (prioridad_valida(nueva)) {
            shared->prioridad_enemy = nueva;
            printf("[P0] Prioridad de Enemigos actualizada oficialmente a %d\n",
                   shared->prioridad_enemy);
        } else {
            printf("[P0] Solicitud rechazada: prioridad Enemigos fuera de rango\n");
        }

        shared->pending_priority_enemy = 0;
        shared->enemy_priority_request_active = 0;
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

int elegir_turno_por_prioridad(SharedData *shared, int *ultimo_turno) {
    printf("[P0] Prioridad Pac-Man=%d | Prioridad Enemigos=%d\n",
           shared->prioridad_pacman,
           shared->prioridad_enemy);

    if (shared->prioridad_pacman > shared->prioridad_enemy) {
        printf("[P0] Pac-Man tiene mayor prioridad\n");
        *ultimo_turno = 1;
        return 1;
    }

    if (shared->prioridad_enemy > shared->prioridad_pacman) {
        printf("[P0] Enemigos tienen mayor prioridad\n");
        *ultimo_turno = 2;
        return 2;
    }

    printf("[P0] Empate de prioridades: aplicando Round Robin\n");

    if (*ultimo_turno == 1) {
        *ultimo_turno = 2;
        return 2;
    }

    *ultimo_turno = 1;
    return 1;
}

void procesar_colision_si_existe(SharedData *shared) {
    if (shared->collision_detected == 1) {
        printf("[P0] Evento de colisión recibido\n");
        printf("[P0] collision_tick=%d\n", shared->collision_tick);
        printf("[P0] collision_ghost_id=%d\n", shared->collision_ghost_id);

        shared->pacman_lives--;

        printf("[P0] Vidas restantes de Pac-Man: %d\n",
               shared->pacman_lives);

        if (shared->pacman_lives <= 0) {
            shared->game_over = 1;
            printf("[P0] Pac-Man perdió todas sus vidas\n");
            printf("[P0] game_over = 1\n");
        }

        shared->collision_detected = 0;
        shared->collision_tick = -1;
        shared->collision_ghost_id = -1;
    }
}

void imprimir_estado_tick(SharedData *shared) {
    printf("[P0] Estado después del tick %d:\n", shared->global_tick);
    printf("     Pac-Man=(%d,%d) | vidas=%d | score=%d | game_over=%d\n",
           shared->pacman_y,
           shared->pacman_x,
           shared->pacman_lives,
           shared->pacman_score,
           shared->game_over);
}

/*
    Funciones de cola interna de P1
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

void destruir_pacman_thread_data(PacmanThreadData *data) {
    pthread_mutex_destroy(&data->mutex_cola);

    sem_destroy(&data->sem_hay_movimientos);
    sem_destroy(&data->sem_hay_espacio);
    sem_destroy(&data->sem_estado_pacman_listo);
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

        pthread_mutex_lock(&data->mutex_cola);
        data->lector_termino = 1;
        pthread_mutex_unlock(&data->mutex_cola);

        sem_post(&data->sem_hay_movimientos);

        return NULL;
    }

    char movimiento[MAX_MOVE];

    while (data->terminar == 0 &&
           leer_movimiento(archivo_pacman, movimiento, sizeof(movimiento))) {

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

        if (shared->game_over == 1) {
            data->terminar = 1;
            sem_post(&data->sem_estado_pacman_listo);
            break;
        }

        printf("[P1-executor] Turno recibido en tick %d\n",
               shared->global_tick);

        char movimiento[MAX_MOVE];

        if (cola_sacar_movimiento(data, movimiento)) {
            int nueva_prioridad;

            if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                solicitar_prioridad_pacman(shared, nueva_prioridad);
            } else {
                mover_pacman(shared, movimiento);
            }
        } else {
            printf("[P1-executor] No hay más movimientos de Pac-Man\n");
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

        if (data->terminar == 1 || shared->game_over == 1) {
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

/*
    P1 = pacman_process

    Ahora P1 crea 3 hilos internos.
*/
void pacman_process(SharedData *shared, const char *carpeta_caso) {
    printf("[P1] pacman_process iniciado\n");
    printf("[P1] PID=%d | PPID=%d\n", getpid(), getppid());

    char ruta_pacman[256];
    construir_ruta(ruta_pacman, sizeof(ruta_pacman), carpeta_caso, "pacman_moves.txt");

    PacmanThreadData data;
    inicializar_pacman_thread_data(&data, shared, ruta_pacman);

    pthread_t hilo_reader;
    pthread_t hilo_executor;
    pthread_t hilo_publisher;

    pthread_create(&hilo_reader, NULL, movement_reader_thread, &data);
    pthread_create(&hilo_executor, NULL, movement_executor_thread, &data);
    pthread_create(&hilo_publisher, NULL, pacman_publisher_thread, &data);

    pthread_join(hilo_executor, NULL);

    data.terminar = 1;

    /*
        Liberamos al reader por si estuviera esperando espacio.
    */
    for (int i = 0; i < P1_QUEUE_SIZE; i++) {
        sem_post(&data.sem_hay_espacio);
    }

    sem_post(&data.sem_hay_movimientos);
    sem_post(&data.sem_estado_pacman_listo);

    pthread_join(hilo_reader, NULL);
    pthread_join(hilo_publisher, NULL);

    destruir_pacman_thread_data(&data);

    printf("[P1] pacman_process finalizado\n");
    exit(0);
}

/*
    Inicializa la estructura interna de P2.
*/
void inicializar_enemy_thread_data(
    EnemyThreadData *data,
    SharedData *shared,
    const char *carpeta_caso
) {
    data->shared = shared;
    data->terminar = 0;

    inicializar_fantasmas_desde_shared(shared, data->ghosts);

    construir_ruta(data->rutas_ghost[0], sizeof(data->rutas_ghost[0]),
                   carpeta_caso, "ghost_1_moves.txt");

    construir_ruta(data->rutas_ghost[1], sizeof(data->rutas_ghost[1]),
                   carpeta_caso, "ghost_2_moves.txt");

    construir_ruta(data->rutas_ghost[2], sizeof(data->rutas_ghost[2]),
                   carpeta_caso, "ghost_3_moves.txt");

    construir_ruta(data->rutas_ghost[3], sizeof(data->rutas_ghost[3]),
                   carpeta_caso, "ghost_4_moves.txt");

    data->pacman_last_y = shared->pacman_y;
    data->pacman_last_x = shared->pacman_x;

    pthread_mutex_init(&data->mutex_ghosts, NULL);
    pthread_mutex_init(&data->mutex_pacman_local, NULL);

    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_init(&data->sem_ghost_turn[i], 0, 0);
        sem_init(&data->sem_ghost_done[i], 0, 0);
    }

    sem_init(&data->sem_tracker_start, 0, 0);
    sem_init(&data->sem_tracker_done, 0, 0);

    sem_init(&data->sem_collision_start, 0, 0);
    sem_init(&data->sem_collision_done, 0, 0);
}

/*
    Libera recursos internos de P2.
*/
void destruir_enemy_thread_data(EnemyThreadData *data) {
    pthread_mutex_destroy(&data->mutex_ghosts);
    pthread_mutex_destroy(&data->mutex_pacman_local);

    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_destroy(&data->sem_ghost_turn[i]);
        sem_destroy(&data->sem_ghost_done[i]);
    }

    sem_destroy(&data->sem_tracker_start);
    sem_destroy(&data->sem_tracker_done);

    sem_destroy(&data->sem_collision_start);
    sem_destroy(&data->sem_collision_done);
}

/*
    Hilo auxiliar general para fantasmas.

    Los wrappers ghost_thread_1, ghost_thread_2, etc.
    llaman a esta función.
*/
void *ghost_thread_generico(void *arg) {
    GhostThreadArg *ghost_arg = (GhostThreadArg *)arg;
    EnemyThreadData *data = ghost_arg->data;
    SharedData *shared = data->shared;
    int id = ghost_arg->ghost_index;

    FILE *archivo = fopen(data->rutas_ghost[id], "r");

    if (archivo == NULL) {
        printf("[P2-ghost-%d] No se pudo abrir %s\n",
               id + 1,
               data->rutas_ghost[id]);
    }

    printf("[P2-ghost-%d] ghost_thread_%d iniciado\n",
           id + 1,
           id + 1);

    while (1) {
        sem_wait(&data->sem_ghost_turn[id]);

        if (data->terminar == 1 || shared->game_over == 1) {
            break;
        }

        char movimiento[MAX_MOVE];

        if (leer_movimiento(archivo, movimiento, sizeof(movimiento))) {
            int nueva_prioridad;

            if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                solicitar_prioridad_enemy(shared, nueva_prioridad);
            } else {
                pthread_mutex_lock(&data->mutex_ghosts);

                mover_fantasma(shared, &data->ghosts[id], movimiento);

                pthread_mutex_unlock(&data->mutex_ghosts);
            }
        } else {
            printf("[P2-ghost-%d] Fantasma %c no tiene más movimientos\n",
                   id + 1,
                   data->ghosts[id].simbolo);
        }

        sem_post(&data->sem_ghost_done[id]);
    }

    if (archivo != NULL) {
        fclose(archivo);
    }

    printf("[P2-ghost-%d] ghost_thread_%d finalizado\n",
           id + 1,
           id + 1);

    return NULL;
}

/*
    Wrappers con los nombres pedidos por la profesora.
*/
void *ghost_thread_1(void *arg) {
    return ghost_thread_generico(arg);
}

void *ghost_thread_2(void *arg) {
    return ghost_thread_generico(arg);
}

void *ghost_thread_3(void *arg) {
    return ghost_thread_generico(arg);
}

void *ghost_thread_4(void *arg) {
    return ghost_thread_generico(arg);
}

/*
    Hilo pacman_tracker_thread.

    Lee la posición actual de Pac-Man desde memoria compartida
    y guarda una copia local dentro de P2.
*/
void *pacman_tracker_thread(void *arg) {
    EnemyThreadData *data = (EnemyThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P2-tracker] pacman_tracker_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_tracker_start);

        if (data->terminar == 1 || shared->game_over == 1) {
            break;
        }

        pthread_mutex_lock(&shared->mutex_shared);

        int y = shared->pacman_y;
        int x = shared->pacman_x;

        pthread_mutex_unlock(&shared->mutex_shared);

        pthread_mutex_lock(&data->mutex_pacman_local);

        data->pacman_last_y = y;
        data->pacman_last_x = x;

        pthread_mutex_unlock(&data->mutex_pacman_local);

        printf("[P2-tracker] Copia local de Pac-Man: (%d,%d)\n", y, x);

        sem_post(&data->sem_tracker_done);
    }

    printf("[P2-tracker] pacman_tracker_thread finalizado\n");

    return NULL;
}

/*
    Hilo collision_thread.

    Compara la copia local de Pac-Man con las posiciones internas
    de los fantasmas.
*/
void *collision_thread(void *arg) {
    EnemyThreadData *data = (EnemyThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P2-collision] collision_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_collision_start);

        if (data->terminar == 1 || shared->game_over == 1) {
            break;
        }

        pthread_mutex_lock(&data->mutex_pacman_local);

        int pacman_y = data->pacman_last_y;
        int pacman_x = data->pacman_last_x;

        pthread_mutex_unlock(&data->mutex_pacman_local);

        int colision = 0;
        int ghost_id = -1;
        char ghost_simbolo = '?';

        pthread_mutex_lock(&data->mutex_ghosts);

        for (int i = 0; i < NUM_GHOSTS; i++) {
            if (pacman_y == data->ghosts[i].y &&
                pacman_x == data->ghosts[i].x) {

                colision = 1;
                ghost_id = data->ghosts[i].id;
                ghost_simbolo = data->ghosts[i].simbolo;
                break;
            }
        }

        pthread_mutex_unlock(&data->mutex_ghosts);

        pthread_mutex_lock(&shared->mutex_shared);

        if (colision == 1) {
            printf("\n[P2-collision] COLISIÓN con fantasma %c\n",
                   ghost_simbolo);

            shared->collision_detected = 1;
            shared->collision_tick = shared->global_tick;
            shared->collision_ghost_id = ghost_id;
        } else {
            shared->collision_detected = 0;
            shared->collision_tick = -1;
            shared->collision_ghost_id = -1;
        }

        pthread_mutex_unlock(&shared->mutex_shared);

        sem_post(&data->sem_collision_done);
    }

    printf("[P2-collision] collision_thread finalizado\n");

    return NULL;
}

/*
    Hilo enemy_controller.

    Este hilo espera el permiso de P0.
    Cuando P0 le da turno a P2:
    1. Activa pacman_tracker_thread.
    2. Activa los 4 ghost_thread.
    3. Espera a que todos terminen.
    4. Activa collision_thread.
    5. Avisa a P0 con sem_turn_done.
*/
void *enemy_controller(void *arg) {
    EnemyThreadData *data = (EnemyThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P2-controller] enemy_controller iniciado\n");

    while (1) {
        sem_wait(&shared->sem_enemy_turn);

        if (shared->game_over == 1) {
            data->terminar = 1;
            break;
        }

        printf("[P2-controller] Turno recibido en tick %d\n",
               shared->global_tick);

        /*
            1. Actualizar copia local de Pac-Man.
        */
        sem_post(&data->sem_tracker_start);
        sem_wait(&data->sem_tracker_done);

        /*
            2. Despertar a los 4 fantasmas.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            sem_post(&data->sem_ghost_turn[i]);
        }

        /*
            3. Esperar a que los 4 fantasmas terminen.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            sem_wait(&data->sem_ghost_done[i]);
        }

        /*
            4. Revisar colisión después de que todos se movieron.
        */
        sem_post(&data->sem_collision_start);
        sem_wait(&data->sem_collision_done);

        printf("[P2-controller] Fin de turno\n");

        /*
            5. Avisar a P0 que P2 terminó.
        */
        sem_post(&shared->sem_turn_done);
    }

    /*
        Liberamos todos los hilos que podrían estar bloqueados.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_post(&data->sem_ghost_turn[i]);
    }

    sem_post(&data->sem_tracker_start);
    sem_post(&data->sem_collision_start);

    printf("[P2-controller] enemy_controller finalizado\n");

    return NULL;
}



/*
    P2 = enemy_process

    En este checkpoint P2 todavía se mantiene igual.
    Sus hilos internos vienen en el siguiente checkpoint.
*/
/*
    P2 = enemy_process

    Ahora P2 crea hilos internos:
    - enemy_controller
    - ghost_thread_1
    - ghost_thread_2
    - ghost_thread_3
    - ghost_thread_4
    - pacman_tracker_thread
    - collision_thread
*/
void enemy_process(SharedData *shared, const char *carpeta_caso) {
    printf("[P2] enemy_process iniciado\n");
    printf("[P2] PID=%d | PPID=%d\n", getpid(), getppid());

    EnemyThreadData data;
    inicializar_enemy_thread_data(&data, shared, carpeta_caso);

    GhostThreadArg ghost_args[NUM_GHOSTS];

    for (int i = 0; i < NUM_GHOSTS; i++) {
        ghost_args[i].data = &data;
        ghost_args[i].ghost_index = i;
    }

    pthread_t hilo_controller;
    pthread_t hilo_ghost_1;
    pthread_t hilo_ghost_2;
    pthread_t hilo_ghost_3;
    pthread_t hilo_ghost_4;
    pthread_t hilo_tracker;
    pthread_t hilo_collision;

    pthread_create(&hilo_controller, NULL, enemy_controller, &data);

    pthread_create(&hilo_ghost_1, NULL, ghost_thread_1, &ghost_args[0]);
    pthread_create(&hilo_ghost_2, NULL, ghost_thread_2, &ghost_args[1]);
    pthread_create(&hilo_ghost_3, NULL, ghost_thread_3, &ghost_args[2]);
    pthread_create(&hilo_ghost_4, NULL, ghost_thread_4, &ghost_args[3]);

    pthread_create(&hilo_tracker, NULL, pacman_tracker_thread, &data);
    pthread_create(&hilo_collision, NULL, collision_thread, &data);

    pthread_join(hilo_controller, NULL);

    data.terminar = 1;

    /*
        Liberamos por seguridad los hilos internos.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_post(&data.sem_ghost_turn[i]);
    }

    sem_post(&data.sem_tracker_start);
    sem_post(&data.sem_collision_start);

    pthread_join(hilo_ghost_1, NULL);
    pthread_join(hilo_ghost_2, NULL);
    pthread_join(hilo_ghost_3, NULL);
    pthread_join(hilo_ghost_4, NULL);

    pthread_join(hilo_tracker, NULL);
    pthread_join(hilo_collision, NULL);

    destruir_enemy_thread_data(&data);

    printf("[P2] enemy_process finalizado\n");
    exit(0);
}
/*
    P0 = scheduler_process

    Este es el proceso principal.
    Crea memoria compartida, carga el mapa, crea P1 y P2,
    controla los ticks, decide turnos y espera fin de turno.
*/
void scheduler_process(const char *carpeta_caso) {
    printf("Pac-Man concurrente POSIX - Checkpoint 12\n");
    printf("[P0] scheduler_process con P1 y P2 multihilo\n");
    printf("[P0] PID=%d\n", getpid());

    /*
        1. Crear e inicializar memoria compartida.
    */
    SharedData *shared = crear_memoria_compartida();
    inicializar_shared(shared);

    /*
        2. Construir ruta del mapa.
    */
    char ruta_mapa[256];
    construir_ruta(ruta_mapa, sizeof(ruta_mapa), carpeta_caso, "map.txt");

    printf("[P0] Leyendo mapa: %s\n", ruta_mapa);

    /*
        3. Cargar mapa en memoria compartida.
    */
    if (cargar_mapa(ruta_mapa, shared) != 0) {
        printf("[ERROR] No se pudo cargar el mapa\n");
        liberar_memoria_compartida(shared);
        exit(1);
    }

    imprimir_mapa(shared);

    printf("[P0] Estado compartido base inicializado\n");
    printf("[P0] Pac-Man en shared: (%d,%d)\n",
           shared->pacman_y,
           shared->pacman_x);

    printf("[P0] Vidas iniciales: %d\n",
           shared->pacman_lives);

    printf("[P0] max_ticks = %d\n",
           shared->max_ticks);

    printf("[P0] Prioridades iniciales: Pac-Man=%d, Enemigos=%d\n",
           shared->prioridad_pacman,
           shared->prioridad_enemy);

    /*
        4. Crear P1 = pacman_process.
    */
    pid_t pid_pacman = fork();

    if (pid_pacman < 0) {
        perror("[P0] Error al crear P1");
        liberar_memoria_compartida(shared);
        exit(1);
    }

    if (pid_pacman == 0) {
        pacman_process(shared, carpeta_caso);
    }

    /*
        5. Crear P2 = enemy_process.
    */
    pid_t pid_enemy = fork();

    if (pid_enemy < 0) {
        perror("[P0] Error al crear P2");
        liberar_memoria_compartida(shared);
        exit(1);
    }

    if (pid_enemy == 0) {
        enemy_process(shared, carpeta_caso);
    }

    printf("[P0] Procesos creados: P1 PID=%d, P2 PID=%d\n",
           pid_pacman,
           pid_enemy);

    /*
        ultimo_turno sirve para Round Robin.
        Lo iniciamos en 2 para que, si hay empate,
        el primer turno sea P1.
    */
    int ultimo_turno = 2;

    /*
        6. Ciclo principal del scheduler.
    */
    while (shared->game_over == 0 &&
           shared->global_tick < shared->max_ticks) {

        shared->global_tick++;

        printf("\n[P0] ==============================\n");
        printf("[P0] Tick global %d\n", shared->global_tick);

        /*
            P0 procesa solicitudes SET_PRIORITY al inicio del tick.
        */
        procesar_solicitudes_prioridad(shared);

        /*
            P0 decide quién recibe turno.
        */
        int turno = elegir_turno_por_prioridad(shared, &ultimo_turno);

        if (turno == 1) {
            printf("[P0] Turno elegido: P1\n");
            sem_post(&shared->sem_pacman_turn);
        } else {
            printf("[P0] Turno elegido: P2\n");
            sem_post(&shared->sem_enemy_turn);
        }

        /*
            P0 espera a que P1 o P2 termine su turno.
        */
        sem_wait(&shared->sem_turn_done);

        printf("[P0] Fin de turno confirmado\n");

        /*
            P0 procesa colisiones publicadas por P2.
        */
        procesar_colision_si_existe(shared);

        imprimir_estado_tick(shared);
    }

    /*
        7. Revisar causa de finalización.
    */
    if (shared->pacman_lives <= 0) {
        printf("\n[P0] Fin por vidas agotadas\n");
    } else if (shared->global_tick >= shared->max_ticks) {
        printf("\n[P0] Se alcanzó max_ticks\n");
        shared->game_over = 1;
    }

    printf("\n[P0] Condición de finalización detectada\n");
    printf("[P0] game_over = %d\n", shared->game_over);

    /*
        8. Liberar a P1 y P2 si están bloqueados esperando turno.
    */
    sem_post(&shared->sem_pacman_turn);
    sem_post(&shared->sem_enemy_turn);

    /*
        9. Esperar procesos hijos.
    */
    int status_p1;
    int status_p2;

    waitpid(pid_pacman, &status_p1, 0);
    printf("[P0] P1 finalizó correctamente\n");

    waitpid(pid_enemy, &status_p2, 0);
    printf("[P0] P2 finalizó correctamente\n");

    /*
        10. Liberar recursos compartidos.
    */
    liberar_memoria_compartida(shared);

    printf("[P0] Recursos liberados\n");
    printf("Fin de Checkpoint 12\n");
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1\n", argv[0]);
        return 1;
    }

    scheduler_process(argv[1]);

    return 0;
}


