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

/*
    CHECKPOINT 10

    Objetivo:
    - P0 controla ticks globales.
    - P0 decide turnos según prioridades.
    - Si hay empate, P0 aplica Round Robin.
    - P1 y P2 pueden leer SET_PRIORITY <NUMBER>.
    - P1 y P2 NO modifican directamente su prioridad.
    - P1 y P2 dejan una solicitud en memoria compartida.
    - P0 procesa las solicitudes al inicio del siguiente tick.
    - Las solicitudes se protegen con mutex_shared.
    - P2 solo publica colisiones.
    - P0 procesa colisiones, baja vidas y activa game_over.

    Todavía NO implementamos:
    - hilos internos de P1
    - hilos internos de P2
    - mutex fuertes para ghost_positions[]
*/

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

    /*
        Prioridades iniciales.
        Como empiezan iguales, al inicio se aplica Round Robin.
    */
    shared->prioridad_pacman = 30;
    shared->prioridad_enemy = 30;

    /*
        Buzones de solicitud de prioridad.
    */
    shared->pending_priority_pacman = 0;
    shared->priority_request_active = 0;

    shared->pending_priority_enemy = 0;
    shared->enemy_priority_request_active = 0;

    /*
        Mutex compartido entre procesos.
    */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared->mutex_shared, &attr);
    pthread_mutexattr_destroy(&attr);

    /*
        Semáforos compartidos entre procesos.
    */
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

/*
    Detecta instrucciones del tipo:

        SET_PRIORITY 80

    Retorna:
    1 si era SET_PRIORITY.
    0 si era otro movimiento.
*/
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

/*
    P1 NO cambia directamente shared->prioridad_pacman.

    Solo deja una solicitud para P0.
*/
void solicitar_prioridad_pacman(SharedData *shared, int nueva_prioridad) {
    pthread_mutex_lock(&shared->mutex_shared);

    shared->pending_priority_pacman = nueva_prioridad;
    shared->priority_request_active = 1;

    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P1] Solicitud enviada a P0: cambiar prioridad Pac-Man a %d\n",
           nueva_prioridad);
}

/*
    P2 NO cambia directamente shared->prioridad_enemy.

    Solo deja una solicitud para P0.
*/
void solicitar_prioridad_enemy(SharedData *shared, int nueva_prioridad) {
    pthread_mutex_lock(&shared->mutex_shared);

    shared->pending_priority_enemy = nueva_prioridad;
    shared->enemy_priority_request_active = 1;

    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P2] Solicitud enviada a P0: cambiar prioridad Enemigos a %d\n",
           nueva_prioridad);
}

/*
    P0 procesa las solicitudes pendientes.

    Se llama al inicio de cada tick, antes de elegir turno.
*/
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

/*
    Decide qué proceso recibe turno.

    Retorna:
    1 -> P1 Pac-Man
    2 -> P2 Enemigos
*/
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

    /*
        Empate de prioridades:
        aplicamos Round Robin.
    */
    printf("[P0] Empate de prioridades: aplicando Round Robin\n");

    if (*ultimo_turno == 1) {
        *ultimo_turno = 2;
        return 2;
    }

    *ultimo_turno = 1;
    return 1;
}

/*
    P0 procesa la colisión publicada por P2.

    P2 no baja vidas.
    P2 solo publica collision_detected.
*/
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
    P1 = pacman_process
*/
void pacman_process(SharedData *shared, const char *carpeta_caso) {
    printf("[P1] pacman_process iniciado\n");
    printf("[P1] PID=%d | PPID=%d\n", getpid(), getppid());

    char ruta_pacman[256];
    construir_ruta(ruta_pacman, sizeof(ruta_pacman), carpeta_caso, "pacman_moves.txt");

    FILE *archivo_pacman = fopen(ruta_pacman, "r");

    if (archivo_pacman == NULL) {
        printf("[P1] No se pudo abrir %s\n", ruta_pacman);
    }

    while (1) {
        sem_wait(&shared->sem_pacman_turn);

        if (shared->game_over == 1) {
            break;
        }

        printf("[P1] Turno recibido en tick %d\n",
               shared->global_tick);

        char movimiento[MAX_MOVE];

        if (leer_movimiento(archivo_pacman, movimiento, sizeof(movimiento))) {
            int nueva_prioridad;

            if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                solicitar_prioridad_pacman(shared, nueva_prioridad);
            } else {
                mover_pacman(shared, movimiento);
            }
        } else {
            printf("[P1] No hay más movimientos de Pac-Man\n");
        }

        printf("[P1] Fin de turno\n");

        sem_post(&shared->sem_turn_done);
    }

    if (archivo_pacman != NULL) {
        fclose(archivo_pacman);
    }

    printf("[P1] pacman_process finalizado\n");
    exit(0);
}

/*
    P2 = enemy_process
*/
void enemy_process(SharedData *shared, const char *carpeta_caso) {
    printf("[P2] enemy_process iniciado\n");
    printf("[P2] PID=%d | PPID=%d\n", getpid(), getppid());

    GhostState ghosts[NUM_GHOSTS];
    inicializar_fantasmas_desde_shared(shared, ghosts);

    char ruta_ghost_1[256];
    char ruta_ghost_2[256];
    char ruta_ghost_3[256];
    char ruta_ghost_4[256];

    construir_ruta(ruta_ghost_1, sizeof(ruta_ghost_1), carpeta_caso, "ghost_1_moves.txt");
    construir_ruta(ruta_ghost_2, sizeof(ruta_ghost_2), carpeta_caso, "ghost_2_moves.txt");
    construir_ruta(ruta_ghost_3, sizeof(ruta_ghost_3), carpeta_caso, "ghost_3_moves.txt");
    construir_ruta(ruta_ghost_4, sizeof(ruta_ghost_4), carpeta_caso, "ghost_4_moves.txt");

    FILE *archivos_ghost[NUM_GHOSTS];

    archivos_ghost[0] = fopen(ruta_ghost_1, "r");
    archivos_ghost[1] = fopen(ruta_ghost_2, "r");
    archivos_ghost[2] = fopen(ruta_ghost_3, "r");
    archivos_ghost[3] = fopen(ruta_ghost_4, "r");

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (archivos_ghost[i] == NULL) {
            printf("[P2] No se pudo abrir archivo de fantasma %d\n", i + 1);
        }
    }

    while (1) {
        sem_wait(&shared->sem_enemy_turn);

        if (shared->game_over == 1) {
            break;
        }

        printf("[P2] Turno recibido en tick %d\n",
               shared->global_tick);

        for (int i = 0; i < NUM_GHOSTS; i++) {
            char movimiento[MAX_MOVE];

            if (leer_movimiento(archivos_ghost[i], movimiento, sizeof(movimiento))) {
                int nueva_prioridad;

                if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                    solicitar_prioridad_enemy(shared, nueva_prioridad);
                } else {
                    mover_fantasma(shared, &ghosts[i], movimiento);
                }
            } else {
                printf("[P2] Fantasma %c no tiene más movimientos\n",
                       ghosts[i].simbolo);
            }
        }

        /*
            P2 solo publica la colisión.
            No baja vidas.
            No activa game_over.
        */
        verificar_colision(shared, ghosts);

        printf("[P2] Fin de turno\n");

        sem_post(&shared->sem_turn_done);
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (archivos_ghost[i] != NULL) {
            fclose(archivos_ghost[i]);
        }
    }

    printf("[P2] enemy_process finalizado\n");
    exit(0);
}

/*
    P0 = scheduler_process
*/
void scheduler_process(const char *carpeta_caso) {
    printf("Pac-Man concurrente POSIX - Checkpoint 10\n");
    printf("[P0] scheduler_process con SET_PRIORITY mediante buzón\n");
    printf("[P0] PID=%d\n", getpid());

    SharedData *shared = crear_memoria_compartida();
    inicializar_shared(shared);

    char ruta_mapa[256];
    construir_ruta(ruta_mapa, sizeof(ruta_mapa), carpeta_caso, "map.txt");

    printf("[P0] Leyendo mapa: %s\n", ruta_mapa);

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

    pid_t pid_pacman = fork();

    if (pid_pacman < 0) {
        perror("[P0] Error al crear P1");
        liberar_memoria_compartida(shared);
        exit(1);
    }

    if (pid_pacman == 0) {
        pacman_process(shared, carpeta_caso);
    }

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

    while (shared->game_over == 0 &&
           shared->global_tick < shared->max_ticks) {

        shared->global_tick++;

        printf("\n[P0] ==============================\n");
        printf("[P0] Tick global %d\n", shared->global_tick);

        /*
            P0 procesa las solicitudes SET_PRIORITY al inicio del tick.
        */
        procesar_solicitudes_prioridad(shared);

        int turno = elegir_turno_por_prioridad(shared, &ultimo_turno);

        if (turno == 1) {
            printf("[P0] Turno elegido: P1\n");
            sem_post(&shared->sem_pacman_turn);
        } else {
            printf("[P0] Turno elegido: P2\n");
            sem_post(&shared->sem_enemy_turn);
        }

        /*
            P0 espera a que el proceso elegido termine su turno.
        */
        sem_wait(&shared->sem_turn_done);

        printf("[P0] Fin de turno confirmado\n");

        /*
            P0 procesa colisiones después de que P1/P2 terminó su turno.
        */
        procesar_colision_si_existe(shared);

        imprimir_estado_tick(shared);
    }

    if (shared->pacman_lives <= 0) {
        printf("\n[P0] Fin por vidas agotadas\n");
    } else if (shared->global_tick >= shared->max_ticks) {
        printf("\n[P0] Se alcanzó max_ticks\n");
        shared->game_over = 1;
    }

    printf("\n[P0] Condición de finalización detectada\n");
    printf("[P0] game_over = %d\n", shared->game_over);

    /*
        Liberamos a P1 y P2 por si están bloqueados esperando turno.
    */
    sem_post(&shared->sem_pacman_turn);
    sem_post(&shared->sem_enemy_turn);

    int status_p1;
    int status_p2;

    waitpid(pid_pacman, &status_p1, 0);
    printf("[P0] P1 finalizó correctamente\n");

    waitpid(pid_enemy, &status_p2, 0);
    printf("[P0] P2 finalizó correctamente\n");

    liberar_memoria_compartida(shared);

    printf("[P0] Recursos liberados\n");
    printf("Fin de Checkpoint 10\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1\n", argv[0]);
        return 1;
    }

    scheduler_process(argv[1]);

    return 0;
}

