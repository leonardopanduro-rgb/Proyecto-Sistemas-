#include "p1_pacman_process.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "movement_queue.h"

typedef struct {
    shared_state_t *state;
    char moves_path[MAX_PATH_LENGTH];
    movement_queue_t queue; 
} pacman_thread_args_t;

static void build_pacman_moves_path(const char *case_dir, char *output, size_t output_size) {
    snprintf(output, output_size, "%s/%s", case_dir, "pacman_moves.txt");
}

static int line_has_content(const char *line) {
    return line[0] != '\0' && line[0] != '\n' && line[0] != '\r';
}


static movement_cmd_t parse_command(const char *line) {
    movement_cmd_t cmd = {CMD_NONE, 0};
    if (strncmp(line, "UP", 2) == 0) cmd.type = CMD_UP;
    else if (strncmp(line, "DOWN", 4) == 0) cmd.type = CMD_DOWN;
    else if (strncmp(line, "LEFT", 4) == 0) cmd.type = CMD_LEFT;
    else if (strncmp(line, "RIGHT", 5) == 0) cmd.type = CMD_RIGHT;
    else if (sscanf(line, "SET_PRIORITY %d", &cmd.value) == 1) cmd.type = CMD_SET_PRIORITY;
    return cmd;
}

static void *movement_reader_thread(void *arg) {
    pacman_thread_args_t *args = (pacman_thread_args_t *)arg;
    FILE *file = fopen(args->moves_path, "r");
    char line[128];
    int moves = 0; // Recuperamos el contador

    if (file == NULL) return NULL;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (line_has_content(line)) {
            movement_cmd_t cmd = parse_command(line);
            if (cmd.type != CMD_NONE) {
                queue_push(&args->queue, cmd);
                moves++;
            }
        }
    }
    fclose(file);
    
    
    // se empuja comandos vacios al final para que el ejecutor no se quede bloqueado (deadlock) esperando si el archivo se acaba.
    for (int i = 0; i < 50; i++) {
        movement_cmd_t eof_cmd = {CMD_NONE, 0};
        queue_push(&args->queue, eof_cmd);
    }

    // Publicamos en memoria compartida y en consola
    pthread_mutex_lock(&args->state->state_mutex);
    args->state->pacman_moves_loaded = moves;
    pthread_mutex_unlock(&args->state->state_mutex);
    printf("[P1][movement_reader_thread] Instrucciones leídas: %d\n", moves);

    return NULL;
}

static void *movement_executor_thread(void *arg) {
    pacman_thread_args_t *args = (pacman_thread_args_t *)arg;
    shared_state_t *s = args->state;
    int local_round = 1; // RASTREADOR DE RONDA

    while (1) {
        if (sem_wait(&s->sem_pacman_turn) != 0) break;

        pthread_mutex_lock(&s->state_mutex);
        if (s->game_over) {
            pthread_mutex_unlock(&s->state_mutex);
            break;
        }
        
        // ¡NUEVO! Si P0 cambió de ronda, rebobinamos la cola
        if (s->current_round > local_round) {
            queue_reset(&args->queue);
            local_round = s->current_round;
            printf("[P1] ¡Nueva ronda detectada! Rebobinando movimientos de Pac-Man.\n");
        }
        
        s->pacman_turns_executed++;
        pthread_mutex_unlock(&s->state_mutex);
        // Consume de la cola
        movement_cmd_t cmd = queue_pop(&args->queue);

        if (cmd.type == CMD_SET_PRIORITY) {
            // Escribe en el Buzón protegido
            pthread_mutex_lock(&s->priority_mutex);
            s->pending_priority_pacman = cmd.value;
            s->priority_request_active = 1;
            pthread_mutex_unlock(&s->priority_mutex);
            printf("[P1] Solicitud de cambio de prioridad a %d enviada al planificador\n", cmd.value);
        } 
        else if (cmd.type != CMD_NONE) {
            // Lógica Espacial
            pthread_mutex_lock(&s->state_mutex);
            int nx = s->pacman_position.x;
            int ny = s->pacman_position.y;
            
            if (cmd.type == CMD_UP) ny--;
            else if (cmd.type == CMD_DOWN) ny++;
            else if (cmd.type == CMD_LEFT) nx--;
            else if (cmd.type == CMD_RIGHT) nx++;

            // Validar contra el mapa en Memoria Compartida
            char cell = s->map_grid[ny][nx];
            if (cell != 'X') {
                s->pacman_position.x = nx;
                s->pacman_position.y = ny;
                printf("[P1] Pac-Man se mueve a (%d, %d)\n", nx, ny);
            } else {
                printf("[P1] Pac-Man intentó moverse a (%d, %d) pero hay pared (X)\n", nx, ny);
            }
            pthread_mutex_unlock(&s->state_mutex);
        }

        // Avisa a P0 que P1 terminó
        sem_post(&s->sem_turn_done); 
    }
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
    queue_init(&args.queue); // Inicializa la cola de movimientos
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
