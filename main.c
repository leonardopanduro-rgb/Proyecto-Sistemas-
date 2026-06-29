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

/*
    CHECKPOINT 8

    Objetivo:
    - P0 controla el avance del juego mediante ticks.
    - P0 decide a quién dar turno.
    - P0 espera sem_turn_done antes de avanzar.
    - P2 solo publica colisiones.
    - P0 procesa colisiones, baja vidas y activa game_over.

    Todavía NO implementamos:
    - prioridades reales
    - Round Robin formal por empate
    - SET_PRIORITY
    - hilos internos
    - mutex fuertes
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

/*
    En Checkpoint 8 todavía elegimos turno de forma simple:
    tick impar -> P1
    tick par   -> P2

    En Checkpoint 9 esto cambiará a prioridades y Round Robin.
*/
int elegir_turno_basico_por_tick(int tick) {
    if (tick % 2 == 1) {
        return 1;   // P1
    }

    return 2;       // P2
}

/*
    P0 procesa la colisión publicada por P2.

    Regla importante:
    P2 NO baja vidas.
    P2 NO activa game_over.
    P2 solo publica collision_detected, collision_tick y collision_ghost_id.
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

        /*
            Limpiamos el evento para no procesar la misma colisión dos veces.
        */
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
            mover_pacman(shared, movimiento);
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
                mover_fantasma(shared, &ghosts[i], movimiento);
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
    printf("Pac-Man concurrente POSIX - Checkpoint 8\n");
    printf("[P0] scheduler_process con ticks globales\n");
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
        Ciclo principal del scheduler.
    */
    while (shared->game_over == 0 &&
           shared->global_tick < shared->max_ticks) {

        shared->global_tick++;

        printf("\n[P0] ==============================\n");
        printf("[P0] Tick global %d\n", shared->global_tick);

        int turno = elegir_turno_basico_por_tick(shared->global_tick);

        if (turno == 1) {
            printf("[P0] Turno elegido: P1\n");
            sem_post(&shared->sem_pacman_turn);
        } else {
            printf("[P0] Turno elegido: P2\n");
            sem_post(&shared->sem_enemy_turn);
        }

        /*
            P0 espera a que P1 o P2 terminen.
            Si quitamos esto, los ticks pueden avanzar sin control.
        */
        sem_wait(&shared->sem_turn_done);

        printf("[P0] Fin de turno confirmado\n");

        /*
            Después de cada turno, P0 revisa si P2 publicó una colisión.
        */
        procesar_colision_si_existe(shared);

        imprimir_estado_tick(shared);
    }

    if (shared->global_tick >= shared->max_ticks) {
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
    printf("Fin de Checkpoint 8\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1\n", argv[0]);
        return 1;
    }

    scheduler_process(argv[1]);

    return 0;
}

