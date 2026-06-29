#define _GNU_SOURCE

#include "p2_enemy_process.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "movement_queue.h"

typedef struct {
    shared_state_t *state;
    movement_queue_t ghost_queues[NUM_GHOSTS];
    pthread_barrier_t turn_start_barrier;
    pthread_barrier_t turn_end_barrier;
    
    position_t pacman_last_position;
    pthread_mutex_t tracker_mutex; 
    
    // NUEVO: Semáforos para orquestar el hilo de colisiones de forma segura
    sem_t sem_collision_start;
    sem_t sem_collision_done;
} p2_context_t;

typedef struct {
    p2_context_t *ctx;
    int ghost_id;
    char moves_path[MAX_PATH_LENGTH];
} ghost_thread_args_t;

static void build_ghost_moves_path(const char *case_dir, int ghost_id, char *output, size_t output_size) {
    snprintf(output, output_size, "%s/ghost_%d_moves.txt", case_dir, ghost_id + 1);
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

static void *ghost_reader_thread(void *arg) {
    ghost_thread_args_t *args = (ghost_thread_args_t *)arg;
    FILE *file = fopen(args->moves_path, "r");
    char line[128];
    int moves = 0;

    if (file != NULL) {
        while (fgets(line, sizeof(line), file) != NULL) {
            if (line_has_content(line)) {
                movement_cmd_t cmd = parse_command(line);
                if (cmd.type != CMD_NONE) {
                    queue_push(&args->ctx->ghost_queues[args->ghost_id], cmd);
                    moves++;
                }
            }
        }
        fclose(file);
    }

    for (int i = 0; i < 50; i++) {
        movement_cmd_t eof_cmd = {CMD_NONE, 0};
        queue_push(&args->ctx->ghost_queues[args->ghost_id], eof_cmd);
    }

    pthread_mutex_lock(&args->ctx->state->state_mutex);
    args->ctx->state->ghost_moves_loaded[args->ghost_id] = moves;
    pthread_mutex_unlock(&args->ctx->state->state_mutex);

    return NULL;
}

static void *ghost_executor_thread(void *arg) {
    ghost_thread_args_t *args = (ghost_thread_args_t *)arg;
    shared_state_t *s = args->ctx->state;
    int id = args->ghost_id;
    int local_round = 1; // RASTREADOR DE RONDA

    while (1) {
        pthread_barrier_wait(&args->ctx->turn_start_barrier);

        pthread_mutex_lock(&s->state_mutex);
        if (s->game_over) {
            pthread_mutex_unlock(&s->state_mutex);
            break;
        }
        
        // ¡NUEVO! Si P0 cambió de ronda, rebobinamos la cola del fantasma
        if (s->current_round > local_round) {
            queue_reset(&args->ctx->ghost_queues[id]);
            local_round = s->current_round;
            // Solo imprimimos una vez para no saturar la consola (ej. el Fantasma A)
            if (id == 0) printf("[P2] ¡Nueva ronda detectada! Rebobinando movimientos de los Fantasmas.\n");
        }
        pthread_mutex_unlock(&s->state_mutex);

        movement_cmd_t cmd = queue_pop(&args->ctx->ghost_queues[id]);
        // ... (El resto se queda igual, validando cmd y moviéndose)
        if (cmd.type != CMD_NONE && cmd.type != CMD_SET_PRIORITY) {
            pthread_mutex_lock(&s->state_mutex);
            int nx = s->ghost_position[id].x;
            int ny = s->ghost_position[id].y;
            
            if (cmd.type == CMD_UP) ny--;
            else if (cmd.type == CMD_DOWN) ny++;
            else if (cmd.type == CMD_LEFT) nx--;
            else if (cmd.type == CMD_RIGHT) nx++;

            char cell = s->map_grid[ny][nx];
            if (cell != 'X') {
                s->ghost_position[id].x = nx;
                s->ghost_position[id].y = ny;
                printf("[P2] Fantasma %s se mueve a (%d, %d)\n", ghost_label(id), nx, ny);
            }
            pthread_mutex_unlock(&s->state_mutex);
        }

        pthread_barrier_wait(&args->ctx->turn_end_barrier);
    }
    return NULL;
}

static void *pacman_tracker_thread(void *arg) {
    p2_context_t *ctx = (p2_context_t *)arg;
    shared_state_t *s = ctx->state;

    while (1) {
        pthread_barrier_wait(&ctx->turn_start_barrier);

        pthread_mutex_lock(&s->state_mutex);
        if (s->game_over) {
            pthread_mutex_unlock(&s->state_mutex);
            break;
        }
        position_t current_pacman_pos = s->pacman_position;
        pthread_mutex_unlock(&s->state_mutex);

        pthread_mutex_lock(&ctx->tracker_mutex);
        ctx->pacman_last_position = current_pacman_pos;
        pthread_mutex_unlock(&ctx->tracker_mutex);

        pthread_barrier_wait(&ctx->turn_end_barrier);
    }
    return NULL;
}

// --- NUEVO: HILO DE COLISIONES ---
static void *collision_thread(void *arg) {
    p2_context_t *ctx = (p2_context_t *)arg;
    shared_state_t *s = ctx->state;

    while (1) {
        // Espera a que el controlador le de permiso
        sem_wait(&ctx->sem_collision_start);

        pthread_mutex_lock(&s->state_mutex);
        int is_game_over = s->game_over;
        pthread_mutex_unlock(&s->state_mutex);

        if (is_game_over) break;

        // Extrae la última posición de Pac-Man vista por el radar
        pthread_mutex_lock(&ctx->tracker_mutex);
        position_t p_pos = ctx->pacman_last_position;
        pthread_mutex_unlock(&ctx->tracker_mutex);

        // Revisa a los 4 fantasmas
        for (int i = 0; i < NUM_GHOSTS; i++) {
            pthread_mutex_lock(&s->state_mutex);
            position_t g_pos = s->ghost_position[i];
            pthread_mutex_unlock(&s->state_mutex);

            // ¿Están en la misma coordenada?
            if (p_pos.x == g_pos.x && p_pos.y == g_pos.y) {
                pthread_mutex_lock(&s->collision_mutex);
                s->collision_detected = 1;
                s->collision_ghost_id = i;
                s->collision_tick = s->global_tick;
                pthread_mutex_unlock(&s->collision_mutex);
                
                // Si ya atrapó a Pac-Man, dejamos de buscar
                break; 
            }
        }
        // Avisa al controlador que terminó de revisar
        sem_post(&ctx->sem_collision_done);
    }
    printf("[P2][collision_thread] game_over recibido, hilo termina\n");
    return NULL;
}

static void *enemy_controller_thread(void *arg) {
    p2_context_t *ctx = (p2_context_t *)arg;
    shared_state_t *s = ctx->state;

    while (1) {
        if (sem_wait(&s->sem_enemy_turn) != 0) break;

        pthread_mutex_lock(&s->state_mutex);
        int is_game_over = s->game_over;
        if (!is_game_over) s->ghost_turns_executed++;
        pthread_mutex_unlock(&s->state_mutex);

        pthread_barrier_wait(&ctx->turn_start_barrier);

        if (is_game_over) {
            // Despierta al colisionador para que pueda terminar limpio
            sem_post(&ctx->sem_collision_start);
            break;
        }

        pthread_barrier_wait(&ctx->turn_end_barrier);

        // ¡NUEVA ORQUESTACIÓN! Todos los fantasmas se movieron. Turno del colisionador.
        sem_post(&ctx->sem_collision_start);
        sem_wait(&ctx->sem_collision_done);

        sem_post(&s->sem_turn_done);
    }
    printf("[P2][enemy_controller_thread] game_over recibido, hilo termina\n");
    return NULL;
}

void enemy_process_bootstrap(const shared_state_t *state) {
    (void)state;
    printf("[P2] %s listo para controlar %d fantasmas\n", process_name(PROCESS_ENEMY), NUM_GHOSTS);
}

int enemy_process_main(shared_state_t *state, const char *case_dir) {
    p2_context_t ctx;
    ctx.state = state;

    pthread_mutex_init(&ctx.tracker_mutex, NULL);
    sem_init(&ctx.sem_collision_start, 0, 0);
    sem_init(&ctx.sem_collision_done, 0, 0);

    int active_ghosts = NUM_GHOSTS; 
    int total_threads_in_barrier = 1 + 1 + active_ghosts;
    
    pthread_barrier_init(&ctx.turn_start_barrier, NULL, total_threads_in_barrier);
    pthread_barrier_init(&ctx.turn_end_barrier, NULL, total_threads_in_barrier);

    pthread_t controller_thread;
    pthread_t tracker_thread;
    pthread_t collision_thread_id;
    pthread_t ghost_readers[NUM_GHOSTS];
    pthread_t ghost_executors[NUM_GHOSTS];
    ghost_thread_args_t ghost_args[NUM_GHOSTS];

    setbuf(stdout, NULL);
    printf("[P2] Proceso real iniciado con PID %ld\n", (long)getpid());

    for (int i = 0; i < active_ghosts; i++) {
        queue_init(&ctx.ghost_queues[i]);
        ghost_args[i].ctx = &ctx;
        ghost_args[i].ghost_id = i;
        build_ghost_moves_path(case_dir, i, ghost_args[i].moves_path, sizeof(ghost_args[i].moves_path));

        pthread_create(&ghost_readers[i], NULL, ghost_reader_thread, &ghost_args[i]);
        pthread_create(&ghost_executors[i], NULL, ghost_executor_thread, &ghost_args[i]);
    }

    pthread_create(&tracker_thread, NULL, pacman_tracker_thread, &ctx);
    pthread_create(&collision_thread_id, NULL, collision_thread, &ctx);
    pthread_create(&controller_thread, NULL, enemy_controller_thread, &ctx);

    for (int i = 0; i < active_ghosts; i++) {
        pthread_join(ghost_readers[i], NULL);
        pthread_join(ghost_executors[i], NULL);
    }
    pthread_join(tracker_thread, NULL);
    pthread_join(collision_thread_id, NULL);
    pthread_join(controller_thread, NULL);

    pthread_barrier_destroy(&ctx.turn_start_barrier);
    pthread_barrier_destroy(&ctx.turn_end_barrier);
    pthread_mutex_destroy(&ctx.tracker_mutex);
    sem_destroy(&ctx.sem_collision_start);
    sem_destroy(&ctx.sem_collision_done);

    printf("[P2] Proceso de fantasmas termina ordenadamente\n");
    return 0;
}