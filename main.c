#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#include "shared.h"
#include "map.h"
#include "pacman.h"
#include "ghost.h"

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
    shared->max_ticks = 20;
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

    /*
        Inicialización básica del mutex.
        Todavía no lo usamos en Checkpoint 5, pero queda listo.
    */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared->mutex_shared, &attr);
    pthread_mutexattr_destroy(&attr);
}

void liberar_memoria_compartida(SharedData *shared) {
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
    }

    if (strlen(movimiento) == 0) {
        return 0;
    }

    return 1;
}

void imprimir_estado_shared(SharedData *shared) {
    printf("\n===== Estado en memoria compartida =====\n");
    printf("global_tick: %d\n", shared->global_tick);
    printf("max_ticks: %d\n", shared->max_ticks);
    printf("game_over: %d\n", shared->game_over);
    printf("Pac-Man: (%d,%d)\n", shared->pacman_y, shared->pacman_x);
    printf("Puntaje Pac-Man: %d\n", shared->pacman_score);
    printf("Vidas Pac-Man: %d\n", shared->pacman_lives);
    printf("Prioridad Pac-Man: %d\n", shared->prioridad_pacman);
    printf("Prioridad Enemigos: %d\n", shared->prioridad_enemy);
    printf("collision_detected: %d\n", shared->collision_detected);
    printf("collision_tick: %d\n", shared->collision_tick);
    printf("collision_ghost_id: %d\n", shared->collision_ghost_id);
    printf("========================================\n\n");
}

void ejecutar_simulacion_secuencial_checkpoint5(SharedData *shared, const char *carpeta_caso) {
    GhostState ghosts[NUM_GHOSTS];
    inicializar_fantasmas_desde_shared(shared, ghosts);

    char ruta_pacman[256];
    char ruta_ghost_1[256];
    char ruta_ghost_2[256];
    char ruta_ghost_3[256];
    char ruta_ghost_4[256];

    construir_ruta(ruta_pacman, sizeof(ruta_pacman), carpeta_caso, "pacman_moves.txt");
    construir_ruta(ruta_ghost_1, sizeof(ruta_ghost_1), carpeta_caso, "ghost_1_moves.txt");
    construir_ruta(ruta_ghost_2, sizeof(ruta_ghost_2), carpeta_caso, "ghost_2_moves.txt");
    construir_ruta(ruta_ghost_3, sizeof(ruta_ghost_3), carpeta_caso, "ghost_3_moves.txt");
    construir_ruta(ruta_ghost_4, sizeof(ruta_ghost_4), carpeta_caso, "ghost_4_moves.txt");

    FILE *archivo_pacman = fopen(ruta_pacman, "r");
    FILE *archivos_ghost[NUM_GHOSTS];

    archivos_ghost[0] = fopen(ruta_ghost_1, "r");
    archivos_ghost[1] = fopen(ruta_ghost_2, "r");
    archivos_ghost[2] = fopen(ruta_ghost_3, "r");
    archivos_ghost[3] = fopen(ruta_ghost_4, "r");

    if (archivo_pacman == NULL) {
        printf("[ADVERTENCIA] No se pudo abrir %s\n", ruta_pacman);
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (archivos_ghost[i] == NULL) {
            printf("[ADVERTENCIA] No se pudo abrir archivo de fantasma %d\n", i + 1);
        }
    }

    printf("\n===== Iniciando simulación secuencial Checkpoint 5 =====\n");
    imprimir_fantasmas(ghosts);

    for (int tick = 1; tick <= shared->max_ticks && shared->game_over == 0; tick++) {
        shared->global_tick = tick;

        printf("\n========== TICK %d ==========\n", shared->global_tick);

        int hubo_accion = 0;
        char movimiento[MAX_MOVE];

        if (leer_movimiento(archivo_pacman, movimiento, sizeof(movimiento))) {
            mover_pacman(shared, movimiento);
            hubo_accion = 1;
        } else {
            printf("[Pac-Man] No hay más movimientos\n");
        }

        for (int i = 0; i < NUM_GHOSTS; i++) {
            if (leer_movimiento(archivos_ghost[i], movimiento, sizeof(movimiento))) {
                mover_fantasma(shared, &ghosts[i], movimiento);
                hubo_accion = 1;
            } else {
                printf("[Fantasma %c] No hay más movimientos\n", ghosts[i].simbolo);
            }
        }

        if (detectar_colision(shared, ghosts)) {
            shared->pacman_lives--;

            printf("[P0 temporal secuencial] Vidas restantes: %d\n",
                   shared->pacman_lives);

            if (shared->pacman_lives <= 0) {
                shared->game_over = 1;
                printf("[P0 temporal secuencial] Pac-Man perdió todas sus vidas\n");
            }
        }

        imprimir_estado_shared(shared);
        imprimir_fantasmas(ghosts);

        if (!hubo_accion) {
            printf("[P0 temporal secuencial] No quedan movimientos. Fin de simulación.\n");
            shared->game_over = 1;
        }
    }

    if (archivo_pacman != NULL) {
        fclose(archivo_pacman);
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (archivos_ghost[i] != NULL) {
            fclose(archivos_ghost[i]);
        }
    }

    printf("\n===== Fin simulación secuencial Checkpoint 5 =====\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s cases/Caso1\n", argv[0]);
        return 1;
    }

    printf("Pac-Man concurrente POSIX - Checkpoint 5\n");
    printf("[P0] Inicializando memoria compartida base\n");

    SharedData *shared = crear_memoria_compartida();
    inicializar_shared(shared);

    char ruta_mapa[256];
    construir_ruta(ruta_mapa, sizeof(ruta_mapa), argv[1], "map.txt");

    printf("[P0] Leyendo mapa: %s\n", ruta_mapa);

    if (cargar_mapa(ruta_mapa, shared) != 0) {
        printf("[ERROR] No se pudo cargar el mapa\n");
        liberar_memoria_compartida(shared);
        return 1;
    }

    imprimir_mapa(shared);

    printf("[P0] Memoria compartida inicializada\n");
    printf("[P0] Pac-Man en shared: (%d,%d)\n",
           shared->pacman_y,
           shared->pacman_x);

    printf("[P0] Vidas iniciales: %d\n", shared->pacman_lives);

    printf("[P0] Prioridades iniciales: Pac-Man=%d, Enemigos=%d\n",
           shared->prioridad_pacman,
           shared->prioridad_enemy);

    ejecutar_simulacion_secuencial_checkpoint5(shared, argv[1]);

    liberar_memoria_compartida(shared);

    printf("\nFin de Checkpoint 5\n");

    return 0;
}

