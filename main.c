#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
#include "renderer.h"

#define PRIORIDAD_MIN 1
#define PRIORIDAD_MAX 100

#define P1_QUEUE_SIZE 128

#define LECTURA_FIN 0
#define LECTURA_OK 1
#define LECTURA_INVALIDA -1
#define LECTURA_ERROR -2

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

    /*
        Punto 2 (colisiones completas).
        Guardamos las posiciones anteriores para detectar el cruce
        (intercambio de posiciones) entre Pac-Man y un fantasma.
    */
    int pacman_previous_y;
    int pacman_previous_x;
    int ghost_previous_y[NUM_GHOSTS];
    int ghost_previous_x[NUM_GHOSTS];

    int terminar;

    pthread_mutex_t mutex_ghosts;
    pthread_mutex_t mutex_pacman_local;
    pthread_mutex_t mutex_terminar;

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

    /*
        BONUS P3: posiciones actuales de los fantasmas.
        Se ponen en las de inicio despues de cargar el mapa.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_x[i] = 0;
        shared->ghost_y[i] = 0;
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

    shared->input_error = 0;
    shared->input_error_process = 0;

    shared->pacman_moves_finished = 0;
    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_moves_finished[i] = 0;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared->mutex_shared, &attr);
    pthread_mutexattr_destroy(&attr);

    sem_init(&shared->sem_pacman_turn, 1, 0);
    sem_init(&shared->sem_enemy_turn, 1, 0);
    sem_init(&shared->sem_turn_done, 1, 0);

    /*
        BONUS P3: semaforos process-shared para sincronizar el renderer.
    */
    sem_init(&shared->sem_render_turn, 1, 0);
    sem_init(&shared->sem_render_done, 1, 0);
}

void liberar_memoria_compartida(SharedData *shared) {
    sem_destroy(&shared->sem_pacman_turn);
    sem_destroy(&shared->sem_enemy_turn);
    sem_destroy(&shared->sem_turn_done);

    sem_destroy(&shared->sem_render_turn);
    sem_destroy(&shared->sem_render_done);

    pthread_mutex_destroy(&shared->mutex_shared);

    if (munmap(shared, sizeof(SharedData)) == -1) {
        perror("Error al liberar memoria compartida");
    }
}

void construir_ruta(char destino[], int tam, const char *carpeta_caso, const char *archivo) {
    snprintf(destino, tam, "%s/%s", carpeta_caso, archivo);
}

/*
    Elimina espacios, tabulaciones, '\n' y '\r' de ambos extremos.
*/
void limpiar_espacios_movimiento(char movimiento[]) {
    int inicio = 0;
    int largo = strlen(movimiento);

    while (movimiento[inicio] != '\0' &&
           isspace((unsigned char)movimiento[inicio])) {
        inicio++;
    }

    while (largo > inicio &&
           isspace((unsigned char)movimiento[largo - 1])) {
        largo--;
    }

    int nuevo_largo = largo - inicio;

    if (inicio > 0 && nuevo_largo > 0) {
        memmove(movimiento, movimiento + inicio, nuevo_largo);
    }

    movimiento[nuevo_largo] = '\0';
}

/*
    Valida la forma de una instruccion sin ejecutarla.
    El rango de prioridad sigue siendo responsabilidad de P0.
*/
int instruccion_movimiento_valida(const char *movimiento) {
    if (strcmp(movimiento, "UP") == 0 ||
        strcmp(movimiento, "DOWN") == 0 ||
        strcmp(movimiento, "LEFT") == 0 ||
        strcmp(movimiento, "RIGHT") == 0) {
        return 1;
    }

    char comando[32];
    char sobrante[32];
    int valor;

    int elementos = sscanf(movimiento,
                           "%31s %d %31s",
                           comando,
                           &valor,
                           sobrante);

    return elementos == 2 && strcmp(comando, "SET_PRIORITY") == 0;
}

/*
    Retorna un estado diferente para instruccion valida, EOF y error.
    Las lineas vacias se ignoran y no terminan la lectura.
*/
int leer_movimiento(FILE *archivo, char movimiento[], int tam) {
    if (archivo == NULL) {
        return LECTURA_ERROR;
    }

    while (fgets(movimiento, tam, archivo) != NULL) {
        /*
            Si no entro el salto de linea y aun no llegamos a EOF,
            la instruccion supera el tamano permitido.
        */
        if (strchr(movimiento, '\n') == NULL && !feof(archivo)) {
            printf("[ERROR] Instruccion demasiado larga\n");
            return LECTURA_INVALIDA;
        }

        limpiar_espacios_movimiento(movimiento);

        if (movimiento[0] == '\0') {
            continue;
        }

        if (!instruccion_movimiento_valida(movimiento)) {
            printf("[ERROR] Instruccion de movimiento invalida: %s\n",
                   movimiento);
            return LECTURA_INVALIDA;
        }

        return LECTURA_OK;
    }

    if (ferror(archivo)) {
        return LECTURA_ERROR;
    }

    return LECTURA_FIN;
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

void publicar_error_entrada(SharedData *shared, int process_id) {
    pthread_mutex_lock(&shared->mutex_shared);

    /*
        Conservamos el primer error para saber que proceso lo origino.
    */
    if (shared->input_error == 0) {
        shared->input_error = 1;
        shared->input_error_process = process_id;
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Solo P0 convierte el error publicado en una condicion de finalizacion.
*/
int procesar_error_entrada(SharedData *shared) {
    int process_id = 0;

    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->input_error == 1) {
        process_id = shared->input_error_process;
        shared->game_over = 1;
    }

    pthread_mutex_unlock(&shared->mutex_shared);

    if (process_id != 0) {
        printf("[P0] Error de entrada reportado por P%d\n", process_id);
        printf("[P0] game_over = 1 por archivo o instruccion invalida\n");
        return 1;
    }

    return 0;
}

/*
    Punto 10.
    P1 avisa que ya no le quedan instrucciones validas.
*/
void publicar_pacman_agotado(SharedData *shared) {
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->pacman_moves_finished == 0) {
        shared->pacman_moves_finished = 1;
        printf("[P1] Sin mas instrucciones: entradas de Pac-Man agotadas\n");
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Punto 10.
    P2 avisa que un fantasma agoto su archivo de movimientos.
*/
void publicar_fantasma_agotado(SharedData *shared, int ghost_id) {
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->ghost_moves_finished[ghost_id] == 0) {
        shared->ghost_moves_finished[ghost_id] = 1;
        printf("[P2] Sin mas instrucciones: fantasma %c agotado\n",
               'A' + ghost_id);
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Punto 10.
    P0 termina cuando P1 y los cuatro fantasmas agotaron sus entradas.
*/
int entradas_agotadas(SharedData *shared) {
    int agotadas = 1;

    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->pacman_moves_finished == 0) {
        agotadas = 0;
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (shared->ghost_moves_finished[i] == 0) {
            agotadas = 0;
        }
    }

    pthread_mutex_unlock(&shared->mutex_shared);

    return agotadas;
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
    /*
        CHECKPOINT 13:
        P0 protege la lectura/escritura del evento de colisión.

        P2 escribe:
        - collision_detected
        - collision_tick
        - collision_ghost_id

        P0 lee esas variables y luego actualiza:
        - pacman_lives
        - game_over
    */
    pthread_mutex_lock(&shared->mutex_shared);

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

    pthread_mutex_unlock(&shared->mutex_shared);
}



void imprimir_estado_tick(SharedData *shared) {
    /*
        CHECKPOINT 13:
        Protegemos la lectura del estado compartido.
    */
    pthread_mutex_lock(&shared->mutex_shared);

    printf("[P0] Estado después del tick %d:\n", shared->global_tick);
    printf("     Pac-Man=(%d,%d) | vidas=%d | score=%d | game_over=%d\n",
           shared->pacman_y,
           shared->pacman_x,
           shared->pacman_lives,
           shared->pacman_score,
           shared->game_over);

    pthread_mutex_unlock(&shared->mutex_shared);
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

        if (shared->game_over == 1) {
            marcar_pacman_terminar(data);
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

        if (pacman_debe_terminar(data) || shared->game_over == 1) {
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

    marcar_pacman_terminar(&data);

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

    data->pacman_previous_y = shared->pacman_y;
    data->pacman_previous_x = shared->pacman_x;

    for (int i = 0; i < NUM_GHOSTS; i++) {
        data->ghost_previous_y[i] = data->ghosts[i].y;
        data->ghost_previous_x[i] = data->ghosts[i].x;
    }

    pthread_mutex_init(&data->mutex_ghosts, NULL);
    pthread_mutex_init(&data->mutex_pacman_local, NULL);
    pthread_mutex_init(&data->mutex_terminar, NULL);

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
/*
    Punto 13.
    El acceso a data->terminar de P2 se centraliza bajo mutex_terminar.
*/
int enemy_debe_terminar(EnemyThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_terminar);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_terminar);

    return valor;
}

void marcar_enemy_terminar(EnemyThreadData *data) {
    pthread_mutex_lock(&data->mutex_terminar);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_terminar);
}

void destruir_enemy_thread_data(EnemyThreadData *data) {
    pthread_mutex_destroy(&data->mutex_ghosts);
    pthread_mutex_destroy(&data->mutex_pacman_local);
    pthread_mutex_destroy(&data->mutex_terminar);

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

        publicar_error_entrada(shared, 2);
    }

    printf("[P2-ghost-%d] ghost_thread_%d iniciado\n",
           id + 1,
           id + 1);

    while (1) {
        sem_wait(&data->sem_ghost_turn[id]);

        if (enemy_debe_terminar(data) || shared->game_over == 1) {
            break;
        }

        char movimiento[MAX_MOVE];
        int estado_lectura = leer_movimiento(archivo,
                                             movimiento,
                                             sizeof(movimiento));

        if (estado_lectura == LECTURA_OK) {
            int nueva_prioridad;

            if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                solicitar_prioridad_enemy(shared, nueva_prioridad);
            } else {
                pthread_mutex_lock(&data->mutex_ghosts);

                mover_fantasma(shared, &data->ghosts[id], movimiento);

                pthread_mutex_unlock(&data->mutex_ghosts);
            }
        } else if (estado_lectura == LECTURA_INVALIDA ||
                   estado_lectura == LECTURA_ERROR) {
            publicar_error_entrada(shared, 2);

            printf("[P2-ghost-%d] Error en archivo de movimientos\n",
                   id + 1);
        } else {
            printf("[P2-ghost-%d] Fantasma %c no tiene más movimientos\n",
                   id + 1,
                   data->ghosts[id].simbolo);
            publicar_fantasma_agotado(shared, id);
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

        if (enemy_debe_terminar(data) || shared->game_over == 1) {
            break;
        }

        pthread_mutex_lock(&shared->mutex_shared);

        int y = shared->pacman_y;
        int x = shared->pacman_x;

        pthread_mutex_unlock(&shared->mutex_shared);

        pthread_mutex_lock(&data->mutex_pacman_local);

        /*
            Antes de sobrescribir, conservamos la posición anterior para
            poder detectar el cruce con un fantasma.
        */
        data->pacman_previous_y = data->pacman_last_y;
        data->pacman_previous_x = data->pacman_last_x;

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

        if (enemy_debe_terminar(data) || shared->game_over == 1) {
            break;
        }

        pthread_mutex_lock(&data->mutex_pacman_local);

        int pacman_y = data->pacman_last_y;
        int pacman_x = data->pacman_last_x;
        int pacman_prev_y = data->pacman_previous_y;
        int pacman_prev_x = data->pacman_previous_x;

        pthread_mutex_unlock(&data->mutex_pacman_local);

        int colision = 0;
        int ghost_id = -1;
        char ghost_simbolo = '?';
        const char *motivo = "";

        /*
            BONUS P3: capturamos la posicion ACTUAL de los cuatro fantasmas
            para publicarla luego en memoria compartida. Se lee aqui, dentro
            de mutex_ghosts, para tener una foto consistente.
        */
        int ghost_pub_y[NUM_GHOSTS];
        int ghost_pub_x[NUM_GHOSTS];

        pthread_mutex_lock(&data->mutex_ghosts);

        for (int i = 0; i < NUM_GHOSTS; i++) {
            ghost_pub_y[i] = data->ghosts[i].y;
            ghost_pub_x[i] = data->ghosts[i].x;
        }

        for (int i = 0; i < NUM_GHOSTS; i++) {
            int gy = data->ghosts[i].y;
            int gx = data->ghosts[i].x;
            int gpy = data->ghost_previous_y[i];
            int gpx = data->ghost_previous_x[i];

            /*
                Cruce: Pac-Man y el fantasma intercambian posiciones.
                Pac-Man actual == posición anterior del fantasma y
                Pac-Man anterior == posición actual del fantasma.
            */
            if (pacman_y == gpy && pacman_x == gpx &&
                pacman_prev_y == gy && pacman_prev_x == gx) {
                colision = 1;
                motivo = "cruce";
            }
            /*
                Choque: el fantasma terminó en la celda de Pac-Man.
            */
            else if (pacman_y == gy && pacman_x == gx) {
                colision = 1;
                motivo = "choque";
            }
            /*
                Celda ocupada: Pac-Man entró a la celda donde estaba el
                fantasma antes de moverse, aunque luego se haya alejado.
            */
            else if (pacman_y == gpy && pacman_x == gpx) {
                colision = 1;
                motivo = "celda ocupada";
            }

            if (colision == 1) {
                ghost_id = data->ghosts[i].id;
                ghost_simbolo = data->ghosts[i].simbolo;
                break;
            }
        }

        pthread_mutex_unlock(&data->mutex_ghosts);

        pthread_mutex_lock(&shared->mutex_shared);

        /*
            BONUS P3: publicar el estado visible de los enemigos (posicion
            actual) bajo el MISMO mutex_shared, en cada turno de P2.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            shared->ghost_y[i] = ghost_pub_y[i];
            shared->ghost_x[i] = ghost_pub_x[i];
        }

        if (colision == 1) {
            printf("\n[P2-collision] COLISIÓN (%s) con fantasma %c\n",
                   motivo,
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
            marcar_enemy_terminar(data);
            break;
        }

        printf("[P2-controller] Turno recibido en tick %d\n",
               shared->global_tick);

        /*
            1. Actualizar copia local de Pac-Man (posición actual y anterior).
        */
        sem_post(&data->sem_tracker_start);
        sem_wait(&data->sem_tracker_done);

        /*
            2. Guardar la posición anterior de cada fantasma antes de moverlo.
               El collision_thread la usará para detectar el cruce.
        */
        pthread_mutex_lock(&data->mutex_ghosts);
        for (int i = 0; i < NUM_GHOSTS; i++) {
            data->ghost_previous_y[i] = data->ghosts[i].y;
            data->ghost_previous_x[i] = data->ghosts[i].x;
        }
        pthread_mutex_unlock(&data->mutex_ghosts);

        /*
            3. Despertar a los 4 fantasmas.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            sem_post(&data->sem_ghost_turn[i]);
        }

        /*
            4. Esperar a que los 4 fantasmas terminen.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            sem_wait(&data->sem_ghost_done[i]);
        }

        /*
            5. Revisar choque y cruce después de que todos se movieron.
        */
        sem_post(&data->sem_collision_start);
        sem_wait(&data->sem_collision_done);

        printf("[P2-controller] Fin de turno\n");

        /*
            6. Avisar a P0 que P2 terminó.
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

    marcar_enemy_terminar(&data);

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
    Punto 2 (Parte B).

    Estructura local de P0 para coordinar sus tres hilos internos.
    La coordinacion es secuencial mediante semaforos, sin busy waiting:

        P0-main -> tick_thread -> scheduler_thread -> signal_thread -> P1/P2
*/
typedef struct {
    SharedData *shared;

    int turno_actual;   /* 1 = P1, 2 = P2 */
    int ultimo_turno;   /* para Round Robin en caso de empate */
    int terminar;

    pthread_mutex_t mutex_scheduler;

    sem_t sem_tick_start;     /* P0-main        -> tick_thread      */
    sem_t sem_tick_ready;     /* tick_thread    -> scheduler_thread */
    sem_t sem_turn_ready;     /* scheduler_thread -> signal_thread  */
    sem_t sem_tick_finished;  /* signal_thread  -> P0-main          */
} SchedulerThreadData;

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

        pthread_mutex_lock(&data->mutex_scheduler);
        data->turno_actual =
            elegir_turno_por_prioridad(shared, &data->ultimo_turno);
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

/*
    P0 = scheduler_process

    Este es el proceso principal.
    Crea memoria compartida, carga el mapa, crea P1 y P2,
    controla los ticks, decide turnos y espera fin de turno.
*/
int scheduler_process(const char *carpeta_caso, int max_ticks_arg, int render_enabled) {
    printf("Pac-Man concurrente POSIX - Checkpoint 13\n");
    printf("[P0] scheduler_process con mitigación de race conditions\n");


    printf("[P0] PID=%d\n", getpid());

    /*
        1. Crear e inicializar memoria compartida.
    */
    SharedData *shared = crear_memoria_compartida();
    inicializar_shared(shared);

    /*
        Punto 10: si se recibio un max_ticks valido, reemplaza al valor
        por defecto fijado en inicializar_shared.
    */
    if (max_ticks_arg > 0) {
        shared->max_ticks = max_ticks_arg;
    }

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
        return 1;
    }

    imprimir_mapa(shared);

    /*
        BONUS P3: dejar visibles las posiciones iniciales de los fantasmas
        para que P3 pueda dibujarlas desde el primer cuadro, antes incluso
        de que P2 tenga su primer turno.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_x[i] = shared->ghost_start_x[i];
        shared->ghost_y[i] = shared->ghost_start_y[i];
    }

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
        return 1;
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
        return 1;
    }

    if (pid_enemy == 0) {
        enemy_process(shared, carpeta_caso);
    }

    /*
        BONUS: Crear P3 = renderer_process (solo si se paso --render).
        Este fork lo ejecuta unicamente P0: P1 y P2 llaman a exit() dentro
        de sus funciones, por lo que nunca llegan a esta linea.
    */
    pid_t pid_render = -1;

    if (render_enabled) {
        pid_render = fork();

        if (pid_render < 0) {
            perror("[P0] Error al crear P3");
            liberar_memoria_compartida(shared);
            return 1;
        }

        if (pid_render == 0) {
            renderer_process(shared);
        }
    }

    printf("[P0] Procesos creados: P1 PID=%d, P2 PID=%d\n",
           pid_pacman,
           pid_enemy);

    if (render_enabled) {
        printf("[P0] Proceso P3 (renderer) PID=%d\n", pid_render);
    }

    /*
        ultimo_turno sirve para Round Robin.
        Lo iniciamos en 2 para que, si hay empate,
        el primer turno sea P1.
    */
    int finalizo_por_error = 0;
    int finalizo_por_agotamiento = 0;

    /*
        Punto 2 (Parte B): P0 crea sus tres hilos internos.
    */
    SchedulerThreadData sched;
    inicializar_scheduler_thread_data(&sched, shared);

    pthread_t hilo_tick;
    pthread_t hilo_scheduler;
    pthread_t hilo_signal;

    pthread_create(&hilo_tick, NULL, tick_thread, &sched);
    pthread_create(&hilo_scheduler, NULL, scheduler_thread, &sched);
    pthread_create(&hilo_signal, NULL, signal_thread, &sched);

    /*
        6. Ciclo principal del scheduler.

        Cada vuelta ejecuta un tick completo a través de la cadena
        tick_thread -> scheduler_thread -> signal_thread -> P1/P2.
    */
    while (shared->game_over == 0 &&
           shared->global_tick < shared->max_ticks) {

        if (procesar_error_entrada(shared)) {
            finalizo_por_error = 1;
            break;
        }

        /*
            Punto 10: si P1 y los cuatro fantasmas agotaron sus entradas,
            P0 finaliza en vez de seguir repartiendo turnos vacios.
        */
        if (entradas_agotadas(shared)) {
            finalizo_por_agotamiento = 1;
            break;
        }

        /*
            Disparar un tick y esperar a que la cadena de hilos lo complete.
        */
        sem_post(&sched.sem_tick_start);
        sem_wait(&sched.sem_tick_finished);

        if (procesar_error_entrada(shared)) {
            finalizo_por_error = 1;
            break;
        }

        /*
            P0 procesa colisiones publicadas por P2.
        */
        procesar_colision_si_existe(shared);

        imprimir_estado_tick(shared);

        /*
            BONUS: pedir a P3 que dibuje el estado de este tick y esperar a
            que termine (exactamente un redibujado por tick). P3 solo lee la
            memoria y toma mutex_shared un instante, asi que no frena a P1/P2.
        */
        if (render_enabled) {
            sem_post(&shared->sem_render_turn);
            sem_wait(&shared->sem_render_done);
        }
    }

    /*
        7. Revisar causa de finalización.
    */
    if (finalizo_por_error) {
        printf("\n[P0] Fin por error de entrada\n");
    } else if (finalizo_por_agotamiento) {
        printf("\n[P0] Fin por agotamiento de entradas de P1 y P2\n");
        shared->game_over = 1;
    } else if (shared->pacman_lives <= 0) {
        printf("\n[P0] Fin por vidas agotadas\n");
    } else if (shared->global_tick >= shared->max_ticks) {
        printf("\n[P0] Se alcanzó max_ticks\n");
        shared->game_over = 1;
    }

    printf("\n[P0] Condición de finalización detectada\n");
    printf("[P0] game_over = %d\n", shared->game_over);

    /*
        8. Detener los tres hilos internos de P0.
           Estan inactivos esperando su semaforo de entrada, asi que
           marcamos terminar y los despertamos uno por uno.
    */
    marcar_scheduler_terminar(&sched);
    sem_post(&sched.sem_tick_start);
    sem_post(&sched.sem_tick_ready);
    sem_post(&sched.sem_turn_ready);

    pthread_join(hilo_tick, NULL);
    pthread_join(hilo_scheduler, NULL);
    pthread_join(hilo_signal, NULL);

    destruir_scheduler_thread_data(&sched);

    /*
        9. Liberar a P1 y P2 si están bloqueados esperando turno.
    */
    sem_post(&shared->sem_pacman_turn);
    sem_post(&shared->sem_enemy_turn);

    /*
        BONUS: liberar a P3 si quedo esperando turno de dibujo. Aqui
        game_over ya vale 1, asi que P3 dibuja el cuadro final y sale.
    */
    if (render_enabled) {
        sem_post(&shared->sem_render_turn);
    }

    /*
        10. Esperar procesos hijos.
    */
    int status_p1;
    int status_p2;

    waitpid(pid_pacman, &status_p1, 0);
    printf("[P0] P1 finalizó correctamente\n");

    waitpid(pid_enemy, &status_p2, 0);
    printf("[P0] P2 finalizó correctamente\n");

    if (render_enabled) {
        int status_p3;
        waitpid(pid_render, &status_p3, 0);
        printf("[P0] P3 finalizó correctamente\n");
    }

    /*
        11. Liberar recursos compartidos.
    */
    liberar_memoria_compartida(shared);

    printf("[P0] Recursos liberados\n");
    printf("Fin de Checkpoint 13\n");

    return finalizo_por_error ? 1 : 0;
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1 [max_ticks] [--render]\n", argv[0]);
        return 1;
    }

    /*
        Punto 10: max_ticks puede recibirse como argumento numerico.
        BONUS: --render (o -r) habilita el proceso P3 (renderer).
        Ambos son opcionales y pueden ir en cualquier orden despues del
        caso. Sin --render, el sistema se comporta como la version base.
    */
    int max_ticks_arg = -1;
    int render_enabled = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--render") == 0 || strcmp(argv[i], "-r") == 0) {
            render_enabled = 1;
        } else {
            int valor = atoi(argv[i]);

            if (valor > 0) {
                max_ticks_arg = valor;
            } else {
                printf("[P0] Argumento invalido '%s'; se ignora\n", argv[i]);
            }
        }
    }

    return scheduler_process(argv[1], max_ticks_arg, render_enabled);
}


