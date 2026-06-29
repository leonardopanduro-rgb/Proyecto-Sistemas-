#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#include "shared.h"
#include "map.h"
#include "pacman.h"
#include "ghost.h"

/*
    CHECKPOINT 6

    En este checkpoint ya creamos los procesos:

    P0 = scheduler_process = proceso padre
    P1 = pacman_process    = proceso hijo Pac-Man
    P2 = enemy_process     = proceso hijo enemigos

    Todavía NO usamos:
    - semáforos
    - hilos
    - scheduler por ticks
    - prioridades dinámicas

    Solo comprobamos que los procesos se crean bien y que pueden leer
    la memoria compartida inicializada por P0.
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
        Este mutex queda preparado para checkpoints posteriores.
        En Checkpoint 6 todavía no lo usamos para proteger secciones críticas.
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

/*
    P1 = pacman_process

    Por ahora solo comprueba que puede leer los datos de Pac-Man
    desde la memoria compartida.
*/
void pacman_process(SharedData *shared) {
    printf("[P1] pacman_process iniciado\n");
    printf("[P1] PID=%d | PPID=%d\n", getpid(), getppid());

    printf("[P1] Lee desde memoria compartida:\n");
    printf("[P1] Posición inicial de Pac-Man: (%d,%d)\n",
           shared->pacman_y,
           shared->pacman_x);

    printf("[P1] Vidas iniciales de Pac-Man: %d\n",
           shared->pacman_lives);

    printf("[P1] Prioridad Pac-Man: %d\n",
           shared->prioridad_pacman);

    /*
        sleep pequeño para que puedas observar procesos con ps o pstree
        si ejecutas comandos rápido en otra terminal.
    */
    sleep(1);

    printf("[P1] pacman_process finalizado\n");

    /*
        Importante:
        El hijo debe terminar aquí para no seguir ejecutando código del padre.
    */
    exit(0);
}

/*
    P2 = enemy_process

    Por ahora solo comprueba que puede leer las posiciones iniciales
    de los fantasmas desde la memoria compartida.
*/
void enemy_process(SharedData *shared) {
    printf("[P2] enemy_process iniciado\n");
    printf("[P2] PID=%d | PPID=%d\n", getpid(), getppid());

    GhostState ghosts[NUM_GHOSTS];
    inicializar_fantasmas_desde_shared(shared, ghosts);

    printf("[P2] Lee desde memoria compartida:\n");

    for (int i = 0; i < NUM_GHOSTS; i++) {
        printf("[P2] Fantasma %c inicia en (%d,%d)\n",
               ghosts[i].simbolo,
               ghosts[i].y,
               ghosts[i].x);
    }

    printf("[P2] Prioridad Enemigos: %d\n",
           shared->prioridad_enemy);

    sleep(1);

    printf("[P2] enemy_process finalizado\n");

    /*
        Importante:
        El hijo debe terminar aquí para no seguir ejecutando código del padre.
    */
    exit(0);
}

/*
    P0 = scheduler_process

    En Checkpoint 6, P0:
    - inicializa memoria compartida
    - carga mapa
    - crea P1 con fork()
    - crea P2 con fork()
    - espera a P1 y P2 con waitpid()
    - libera recursos

    Todavía no planifica turnos.
*/
void scheduler_process(const char *carpeta_caso) {
    printf("Pac-Man concurrente POSIX - Checkpoint 6\n");
    printf("[P0] scheduler_process inicializando arquitectura base\n");
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

    printf("[P0] Prioridades iniciales: Pac-Man=%d, Enemigos=%d\n",
           shared->prioridad_pacman,
           shared->prioridad_enemy);

    /*
        Crear P1 = pacman_process
    */
    pid_t pid_pacman = fork();

    if (pid_pacman < 0) {
        perror("[P0] Error al crear P1 con fork");
        liberar_memoria_compartida(shared);
        exit(1);
    }

    if (pid_pacman == 0) {
        pacman_process(shared);
    }

    /*
        Crear P2 = enemy_process

        OJO:
        Este fork lo hace P0, no P1.
        Por eso P1 usa exit(0) dentro de pacman_process.
    */
    pid_t pid_enemy = fork();

    if (pid_enemy < 0) {
        perror("[P0] Error al crear P2 con fork");
        liberar_memoria_compartida(shared);
        exit(1);
    }

    if (pid_enemy == 0) {
        enemy_process(shared);
    }

    printf("[P0] Procesos creados:\n");
    printf("[P0] P1 PID=%d\n", pid_pacman);
    printf("[P0] P2 PID=%d\n", pid_enemy);

    /*
        P0 espera a que terminen P1 y P2.
        Esto evita procesos zombis.
    */
    int status_p1;
    int status_p2;

    waitpid(pid_pacman, &status_p1, 0);
    printf("[P0] P1 finalizó correctamente\n");

    waitpid(pid_enemy, &status_p2, 0);
    printf("[P0] P2 finalizó correctamente\n");

    liberar_memoria_compartida(shared);

    printf("[P0] Recursos liberados\n");
    printf("Fin de Checkpoint 6\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s Caso1\n", argv[0]);
        return 1;
    }

    scheduler_process(argv[1]);

    return 0;
}

