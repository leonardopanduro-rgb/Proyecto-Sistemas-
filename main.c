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

/*
    CHECKPOINT 7

    En este checkpoint ya existen:

    P0 = scheduler_process
    P1 = pacman_process
    P2 = enemy_process

    Y ahora agregamos semáforos de turno:

    - sem_pacman_turn
    - sem_enemy_turn
    - sem_turn_done

    Todavía NO implementamos:
    - prioridades reales
    - Round Robin formal
    - SET_PRIORITY
    - hilos internos
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
    shared->max_ticks = 8;
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
        Mutex preparado para checkpoints posteriores.
    */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared->mutex_shared, &attr);
    pthread_mutexattr_destroy(&attr);

    /*
        Semáforos compartidos entre procesos.

        Segundo parámetro = 1:
            Significa que el semáforo será compartido entre procesos.

        Valor inicial = 0:
            P1 y P2 empiezan bloqueados hasta que P0 les dé permiso.
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
    P1 = pacman_process

    Ahora P1 ya NO ejecuta libremente.
    Primero espera sem_pacman_turn.
    Luego ejecuta una acción.
    Finalmente avisa a P0 con sem_turn_done.
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
        /*
            P1 se queda bloqueado aquí hasta que P0 le dé turno.
        */
        sem_wait(&shared->sem_pacman_turn);

        if (shared->game_over == 1) {
            break;
        }

        printf("[P1] Turno recibido de P0\n");

        char movimiento[MAX_MOVE];

        if (leer_movimiento(archivo_pacman, movimiento, sizeof(movimiento))) {
            mover_pacman(shared, movimiento);
        } else {
            printf("[P1] No hay más movimientos de Pac-Man\n");
        }

        printf("[P1] Fin de turno\n");

        /*
            P1 avisa a P0 que terminó.
            Sin esto, P0 se queda bloqueado.
        */
        
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

    Ahora P2 también espera permiso de P0.
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
        /*
            P2 se queda bloqueado aquí hasta que P0 le dé turno.
        */
        sem_wait(&shared->sem_enemy_turn);

        if (shared->game_over == 1) {
            break;
        }

        printf("[P2] Turno recibido de P0\n");

        for (int i = 0; i < NUM_GHOSTS; i++) {
            char movimiento[MAX_MOVE];

            if (leer_movimiento(archivos_ghost[i], movimiento, sizeof(movimiento))) {
                mover_fantasma(shared, &ghosts[i], movimiento);
            } else {
                printf("[P2] Fantasma %c no tiene más movimientos\n",
                       ghosts[i].simbolo);
            }
        }

        if (detectar_colision(shared, ghosts)) {
            shared->pacman_lives--;

            printf("[P0 temporal] Vidas restantes: %d\n",
                   shared->pacman_lives);

            if (shared->pacman_lives <= 0) {
                shared->game_over = 1;
                printf("[P0 temporal] game_over activado\n");
            }
        }

        printf("[P2] Fin de turno\n");

        /*
            P2 avisa a P0 que terminó.
        */
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

    En Checkpoint 7 P0 todavía no decide por prioridad.
    Solo alterna turnos para comprobar semáforos:

    Tick impar  -> P1
    Tick par    -> P2
*/
void scheduler_process(const char *carpeta_caso) {
    printf("Pac-Man concurrente POSIX - Checkpoint 7\n");
    printf("[P0] scheduler_process inicializando semáforos de turno\n");
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

    for (int tick = 1; tick <= shared->max_ticks && shared->game_over == 0; tick++) {
        shared->global_tick = tick;

        printf("\n[P0] Tick %d\n", shared->global_tick);

        if (tick % 2 == 1) {
            printf("[P0] Turno elegido: P1\n");
            sem_post(&shared->sem_pacman_turn);
        } else {
            printf("[P0] Turno elegido: P2\n");
            sem_post(&shared->sem_enemy_turn);
        }

        /*
            P0 espera a que el proceso elegido termine.
            Esta es la parte más importante del checkpoint.
        */
        sem_wait(&shared->sem_turn_done);

        printf("[P0] Fin de turno confirmado\n");
    }

    printf("\n[P0] Condición de finalización detectada\n");
    shared->game_over = 1;

    /*
        Liberamos a ambos procesos por si están bloqueados esperando turno.
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
    printf("Fin de Checkpoint 7\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1\n", argv[0]);
        return 1;
    }

    scheduler_process(argv[1]);

    return 0;
}

