#include "p1_pacman_process.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

typedef struct {
    /* state apunta a la memoria compartida creada por P0. */
    shared_state_t *state;

    /* moves_path guarda la ruta hacia pacman_moves.txt. */
    char moves_path[MAX_PATH_LENGTH];
} pacman_thread_args_t;

static void build_pacman_moves_path(const char *case_dir,
                                    char *output,
                                    size_t output_size) {
    /* Construye la ruta del archivo de movimientos de Pac-Man. */
    snprintf(output, output_size, "%s/%s", case_dir, "pacman_moves.txt");
}

static int line_has_content(const char *line) {
    /* Retorna verdadero si la linea no esta vacia. */
    return line[0] != '\0' && line[0] != '\n' && line[0] != '\r';
}

static void *movement_reader_thread(void *arg) {
    /* Convierte el argumento generico void* al tipo real usado por este hilo. */
    pacman_thread_args_t *args = (pacman_thread_args_t *)arg;

    /* Abre el archivo pacman_moves.txt en modo lectura. */
    FILE *file = fopen(args->moves_path, "r");

    /* line guarda temporalmente cada linea leida del archivo. */
    char line[128];

    /* moves cuenta cuantas instrucciones no vacias encontro el hilo. */
    int moves = 0;

    /* Si fopen falla, el hilo reporta el error y termina. */
    if (file == NULL) {
        perror("[P1] fopen pacman_moves.txt");
        return NULL;
    }

    /* Lee el archivo linea por linea hasta llegar al final. */
    while (fgets(line, sizeof(line), file) != NULL) {
        /* Solo cuenta lineas que realmente tienen contenido. */
        if (line_has_content(line)) {
            moves++;
        }
    }

    /* Cierra el archivo porque ya no se necesita. */
    fclose(file);

    /* Bloquea el mutex antes de escribir en memoria compartida. */
    pthread_mutex_lock(&args->state->state_mutex);

    /* Publica cuantas instrucciones de Pac-Man fueron leidas. */
    args->state->pacman_moves_loaded = moves;

    /* Libera el mutex despues de actualizar el estado. */
    pthread_mutex_unlock(&args->state->state_mutex);

    /* Muestra en consola el trabajo realizado por el hilo lector. */
    printf("[P1][movement_reader_thread] Instrucciones leidas: %d\n", moves);

    /* Termina el hilo correctamente. */
    return NULL;
}

static void *movement_executor_thread(void *arg) {
    /* Convierte el argumento generico void* al tipo real usado por este hilo. */
    pacman_thread_args_t *args = (pacman_thread_args_t *)arg;

    /* El hilo se mantiene vivo mientras P0 no marque game_over. */
    while (1) {
        /* Espera bloqueado hasta que P0 libere el turno de Pac-Man. */
        if (sem_wait(&args->state->sem_pacman_turn) != 0) {
            perror("[P1] sem_wait sem_pacman_turn");
            return NULL;
        }

        /* Bloquea el estado compartido antes de leer game_over y global_tick. */
        pthread_mutex_lock(&args->state->state_mutex);

        /* Si P0 ya termino el juego, este hilo sale del bucle. */
        if (args->state->game_over) {
            pthread_mutex_unlock(&args->state->state_mutex);
            break;
        }

        /* Cuenta un turno ejecutado por Pac-Man. */
        args->state->pacman_turns_executed++;

        /* Copia el tick actual para imprimirlo fuera del mutex. */
        int tick = args->state->global_tick;

        /* Copia el numero de turno de P1 para imprimirlo fuera del mutex. */
        int turn = args->state->pacman_turns_executed;

        /* Copia la posicion actual de Pac-Man para imprimirla fuera del mutex. */
        position_t pos = args->state->pacman_position;

        /* Libera el mutex lo antes posible. */
        pthread_mutex_unlock(&args->state->state_mutex);

        /* En este avance todavia no se mueve, solo se demuestra el turno. */
        printf("[P1][movement_executor_thread] Tick %d, turno %d, Pac-Man sigue en (%d,%d)\n",
               tick,
               turn,
               pos.x,
               pos.y);

        /* Avisa a P0 que el turno de P1 ya termino. */
        if (sem_post(&args->state->sem_turn_done) != 0) {
            perror("[P1] sem_post sem_turn_done");
            return NULL;
        }
    }

    /* Mensaje de cierre normal del hilo. */
    printf("[P1][movement_executor_thread] game_over recibido, hilo termina\n");

    /* Termina el hilo correctamente. */
    return NULL;
}

void pacman_process_bootstrap(const shared_state_t *state) {
    printf("[P1] %s listo\n", process_name(PROCESS_PACMAN));
    printf("[P1] Posicion inicial de Pac-Man: (%d,%d)\n",
           state->pacman_position.x,
           state->pacman_position.y);
}

int pacman_process_main(shared_state_t *state, const char *case_dir) {
    /* reader_thread sera el hilo que lee pacman_moves.txt. */
    pthread_t reader_thread;

    /* executor_thread sera el hilo que espera los turnos del scheduler. */
    pthread_t executor_thread;

    /* args contiene datos compartidos entre los hilos de este proceso. */
    pacman_thread_args_t args;

    /* Desactiva buffer de stdout para ver mensajes de P1 en tiempo real. */
    setbuf(stdout, NULL);

    /* Guarda el puntero a memoria compartida dentro de args. */
    args.state = state;

    /* Construye la ruta hacia pacman_moves.txt. */
    build_pacman_moves_path(case_dir, args.moves_path, sizeof(args.moves_path));

    /* Muestra el PID real del proceso P1. */
    printf("[P1] Proceso real iniciado con PID %ld\n", (long)getpid());

    /* Muestra que archivo usara el hilo lector. */
    printf("[P1] Archivo de movimientos: %s\n", args.moves_path);

    /* Crea el hilo que lee el archivo de movimientos. */
    if (pthread_create(&reader_thread, NULL, movement_reader_thread, &args) != 0) {
        perror("[P1] pthread_create movement_reader_thread");
        return 1;
    }

    /* Crea el hilo que espera sem_pacman_turn y ejecuta turnos. */
    if (pthread_create(&executor_thread, NULL, movement_executor_thread, &args) != 0) {
        perror("[P1] pthread_create movement_executor_thread");
        pthread_join(reader_thread, NULL);
        return 1;
    }

    /* Espera que el hilo lector termine. */
    pthread_join(reader_thread, NULL);

    /* Espera que el hilo ejecutor termine cuando P0 marque game_over. */
    pthread_join(executor_thread, NULL);

    /* Mensaje final del proceso P1. */
    printf("[P1] Proceso Pac-Man termina ordenadamente\n");

    /* Retorna 0 para indicar exito al proceso padre P0. */
    return 0;
}
