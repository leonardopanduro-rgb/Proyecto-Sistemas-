#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"

/*
    Estado compartido entre P0, P1, P2 y el renderer P3.

    Regla de sincronizacion:
    despues de los fork, cualquier campo mutable de SharedData debe leerse o
    escribirse bajo mutex_shared, salvo que el acceso ocurra antes de crear los
    hijos. Los helpers de este archivo centralizan las operaciones que antes
    provocaban carreras sobre game_over y global_tick.
*/

/*
    Reserva SharedData en una region anonima compartida.

    MAP_SHARED hace visibles las escrituras a todos los procesos descendientes;
    MAP_ANONYMOUS evita depender de un archivo. Retorna un puntero tipado a la
    region o termina el proceso si mmap falla, porque el juego no puede operar
    sin su estado central.
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

/*
    Inicializa todos los campos y primitivas antes de ejecutar fork().

    El mutex usa PTHREAD_PROCESS_SHARED: un mutex normal solo sincronizaria
    hilos del mismo proceso. Los semaforos de turno usan pshared=1 porque P0
    hace post y P1/P2/P3 hacen wait desde procesos distintos. Todos comienzan
    en cero para que ningun hijo actue sin autorizacion de P0.
*/
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

    /* P0 -> P1: autoriza exactamente un turno de Pac-Man. */
    sem_init(&shared->sem_pacman_turn, 1, 0);
    /* P0 -> P2: autoriza un turno coordinado de los cuatro fantasmas. */
    sem_init(&shared->sem_enemy_turn, 1, 0);
    /* P1/P2 -> P0: confirma que el proceso elegido termino su turno. */
    sem_init(&shared->sem_turn_done, 1, 0);

    /*
        BONUS P3: semaforos process-shared para sincronizar el renderer.
    */
    /* P0 -> P3 solicita un frame; P3 -> P0 confirma que fue consumido. */
    sem_init(&shared->sem_render_turn, 1, 0);
    sem_init(&shared->sem_render_done, 1, 0);
}

/*
    Destruye recursos compartidos en orden seguro y libera mmap.
    Solo P0 debe llamarla, despues de join de sus hilos y waitpid de los hijos;
    destruir un semaforo o mutex con usuarios activos seria comportamiento
    indefinido.
*/
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

/* Une carpeta y archivo con snprintf, respetando la capacidad de destino. */
void construir_ruta(char destino[], int tam, const char *carpeta_caso, const char *archivo) {
    snprintf(destino, tam, "%s/%s", carpeta_caso, archivo);
}

/*
    Devuelve una copia sincronizada de game_over.
    Evita que los hilos de P1/P2 lean la bandera mientras P0 la modifica.
*/
int obtener_game_over(SharedData *shared) {
    int game_over;

    pthread_mutex_lock(&shared->mutex_shared);
    game_over = shared->game_over;
    pthread_mutex_unlock(&shared->mutex_shared);

    return game_over;
}

/* Activa game_over bajo el mismo mutex usado por todos sus lectores. */
void establecer_game_over(SharedData *shared) {
    pthread_mutex_lock(&shared->mutex_shared);
    shared->game_over = 1;
    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Devuelve el tick bajo mutex_shared. Evita una carrera con tick_thread,
    unico escritor de global_tick durante la simulacion.
*/
int obtener_global_tick(SharedData *shared) {
    int tick;

    pthread_mutex_lock(&shared->mutex_shared);
    tick = shared->global_tick;
    pthread_mutex_unlock(&shared->mutex_shared);

    return tick;
}

/*
    Obtiene un snapshot atomico del estado de control.

    Los cuatro valores se copian dentro de una sola seccion critica; P0 no ve
    una combinacion imposible, por ejemplo game_over antiguo con tick nuevo.
    Los parametros de salida deben apuntar a enteros validos del llamador.
*/
void obtener_control_juego(
    SharedData *shared,
    int *game_over,
    int *global_tick,
    int *max_ticks,
    int *pacman_lives
) {
    pthread_mutex_lock(&shared->mutex_shared);

    *game_over = shared->game_over;
    *global_tick = shared->global_tick;
    *max_ticks = shared->max_ticks;
    *pacman_lives = shared->pacman_lives;

    pthread_mutex_unlock(&shared->mutex_shared);
}
