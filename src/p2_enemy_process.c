#include "p2_enemy_process.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

typedef struct {
    /* state apunta a la memoria compartida creada por P0. */
    shared_state_t *state;

    /* ghost_id identifica al fantasma A, B, C o D como 0, 1, 2 o 3. */
    int ghost_id;

    /* moves_path guarda la ruta hacia ghost_N_moves.txt. */
    char moves_path[MAX_PATH_LENGTH];
} ghost_thread_args_t;

typedef struct {
    /* state apunta al estado global usado por el controlador de enemigos. */
    shared_state_t *state;
} enemy_controller_args_t;

static void build_ghost_moves_path(const char *case_dir,
                                   int ghost_id,
                                   char *output,
                                   size_t output_size) {
    /* Construye la ruta del archivo de movimientos de un fantasma. */
    snprintf(output,
             output_size,
             "%s/ghost_%d_moves.txt",
             case_dir,
             ghost_id + 1);
}

static int line_has_content(const char *line) {
    /* Retorna verdadero si la linea leida no esta vacia. */
    return line[0] != '\0' && line[0] != '\n' && line[0] != '\r';
}

static void *ghost_reader_thread(void *arg) {
    /* Convierte el argumento generico void* al tipo real usado por este hilo. */
    ghost_thread_args_t *args = (ghost_thread_args_t *)arg;

    /* Abre el archivo de movimientos del fantasma correspondiente. */
    FILE *file = fopen(args->moves_path, "r");

    /* line guarda temporalmente cada linea del archivo. */
    char line[128];

    /* moves cuenta cuantas instrucciones no vacias se encontraron. */
    int moves = 0;

    /* Si fopen falla, el hilo reporta el error y termina. */
    if (file == NULL) {
        perror("[P2] fopen ghost_moves.txt");
        return NULL;
    }

    /* Lee el archivo linea por linea. */
    while (fgets(line, sizeof(line), file) != NULL) {
        /* Cuenta solo instrucciones con contenido. */
        if (line_has_content(line)) {
            moves++;
        }
    }

    /* Cierra el archivo al terminar de leerlo. */
    fclose(file);

    /* Bloquea el mutex antes de escribir en memoria compartida. */
    pthread_mutex_lock(&args->state->state_mutex);

    /* Publica cuantas instrucciones tiene este fantasma. */
    args->state->ghost_moves_loaded[args->ghost_id] = moves;

    /* Libera el mutex despues de actualizar el estado. */
    pthread_mutex_unlock(&args->state->state_mutex);

    /* Reporta el resultado de lectura del hilo. */
    printf("[P2][ghost_thread_%s] Instrucciones leidas: %d\n",
           ghost_label(args->ghost_id),
           moves);

    /* Termina el hilo correctamente. */
    return NULL;
}

static void *enemy_controller_thread(void *arg) {
    /* Convierte el argumento generico void* al tipo real usado por este hilo. */
    enemy_controller_args_t *args = (enemy_controller_args_t *)arg;

    /* El controlador vive hasta que P0 active game_over. */
    while (1) {
        /* Espera bloqueado hasta que P0 libere el turno de enemigos. */
        if (sem_wait(&args->state->sem_enemy_turn) != 0) {
            perror("[P2] sem_wait sem_enemy_turn");
            return NULL;
        }

        /* Bloquea el estado para leer game_over y actualizar contador de turnos. */
        pthread_mutex_lock(&args->state->state_mutex);

        /* Si el scheduler ya termino, el hilo sale del bucle. */
        if (args->state->game_over) {
            pthread_mutex_unlock(&args->state->state_mutex);
            break;
        }

        /* Cuenta un turno ejecutado por el proceso de enemigos. */
        args->state->ghost_turns_executed++;

        /* Copia el tick actual para imprimirlo fuera del mutex. */
        int tick = args->state->global_tick;

        /* Copia el numero de turno de P2 para imprimirlo fuera del mutex. */
        int turn = args->state->ghost_turns_executed;

        /* Libera el mutex lo antes posible. */
        pthread_mutex_unlock(&args->state->state_mutex);

        /* En este avance aun no mueve fantasmas; solo demuestra el turno. */
        printf("[P2][enemy_controller_thread] Tick %d, turno %d, fantasmas aun sin movimiento real\n",
               tick,
               turn);

        /* Avisa a P0 que P2 ya termino su turno. */
        if (sem_post(&args->state->sem_turn_done) != 0) {
            perror("[P2] sem_post sem_turn_done");
            return NULL;
        }
    }

    /* Mensaje de cierre normal del hilo controlador. */
    printf("[P2][enemy_controller_thread] game_over recibido, hilo termina\n");

    /* Termina el hilo correctamente. */
    return NULL;
}

void enemy_process_bootstrap(const shared_state_t *state) {
    printf("[P2] %s listo para controlar %d fantasmas\n",
           process_name(PROCESS_ENEMY),
           NUM_GHOSTS);

    for (int i = 0; i < NUM_GHOSTS; ++i) {
        printf("[P2] Fantasma %s inicia en (%d,%d)\n",
               ghost_label(i),
               state->ghost_start[i].x,
               state->ghost_start[i].y);
    }
}

int enemy_process_main(shared_state_t *state, const char *case_dir) {
    /* Arreglo de threads lectores, uno por fantasma. */
    pthread_t ghost_threads[NUM_GHOSTS];

    /* Thread controlador que espera el semaforo de turno de enemigos. */
    pthread_t controller_thread;

    /* Argumentos independientes para cada thread de fantasma. */
    ghost_thread_args_t ghost_args[NUM_GHOSTS];

    /* Argumentos del thread controlador. */
    enemy_controller_args_t controller_args;

    /* Desactiva buffer de stdout para ver mensajes de P2 en tiempo real. */
    setbuf(stdout, NULL);

    /* El controlador tambien necesita acceder al estado compartido. */
    controller_args.state = state;

    /* Muestra el PID real del proceso P2. */
    printf("[P2] Proceso real iniciado con PID %ld\n", (long)getpid());

    /* Crea cuatro threads, uno para leer cada archivo de fantasma. */
    for (int i = 0; i < NUM_GHOSTS; ++i) {
        /* Guarda el estado compartido dentro de los argumentos del thread. */
        ghost_args[i].state = state;

        /* Guarda que fantasma le corresponde a este thread. */
        ghost_args[i].ghost_id = i;

        /* Construye la ruta del archivo ghost_N_moves.txt. */
        build_ghost_moves_path(case_dir,
                               i,
                               ghost_args[i].moves_path,
                               sizeof(ghost_args[i].moves_path));

        /* Crea el hilo lector para este fantasma. */
        if (pthread_create(&ghost_threads[i], NULL, ghost_reader_thread, &ghost_args[i]) != 0) {
            perror("[P2] pthread_create ghost_reader_thread");
            return 1;
        }
    }

    /* Crea el hilo que esperara los turnos del scheduler. */
    if (pthread_create(&controller_thread, NULL, enemy_controller_thread, &controller_args) != 0) {
        perror("[P2] pthread_create enemy_controller_thread");

        /* Si falla el controlador, igual espera a los lectores ya creados. */
        for (int i = 0; i < NUM_GHOSTS; ++i) {
            pthread_join(ghost_threads[i], NULL);
        }
        return 1;
    }

    /* Espera a que terminen los cuatro lectores de archivos. */
    for (int i = 0; i < NUM_GHOSTS; ++i) {
        pthread_join(ghost_threads[i], NULL);
    }

    /* Espera a que el controlador termine cuando P0 active game_over. */
    pthread_join(controller_thread, NULL);

    /* Mensaje final del proceso P2. */
    printf("[P2] Proceso de fantasmas termina ordenadamente\n");

    /* Retorna 0 para indicar exito al proceso padre P0. */
    return 0;
}
