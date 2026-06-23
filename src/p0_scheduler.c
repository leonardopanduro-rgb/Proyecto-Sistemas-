#include "p0_scheduler.h"

#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "p2_enemy_process.h"
#include "map.h"
#include "p1_pacman_process.h"
#include "sync_utils.h"

static void build_map_path(const char *case_dir, char *output, size_t output_size) {
    /* Une la carpeta del caso con el nombre fijo del archivo de mapa. */
    snprintf(output, output_size, "%s/%s", case_dir, "map.txt");
}

static void shared_state_init_from_map(shared_state_t *state, const game_map_t *map) {
    /* Limpia toda la estructura antes de copiar datos iniciales del mapa. */
    memset(state, 0, sizeof(*state));

    /* Inicializa los campos globales que P0 controlara durante la simulacion. */
    state->global_tick = 0;
    state->max_ticks = 8;
    state->game_over = 0;

    /* Inicializa datos propios de Pac-Man. */
    state->pacman_score = 0;
    state->pacman_lives = 3;

    /* Se usan prioridades iguales para mostrar Round Robin desde el primer avance. */
    state->prioridad_pacman = 30;
    state->prioridad_enemy = 30;

    /* Copia las dimensiones del mapa validado. */
    state->map_rows = map->rows;
    state->map_cols = map->cols;

    /* Guarda la posicion inicial y actual de Pac-Man. */
    state->pacman_start = map->pacman_start;
    state->pacman_position = map->pacman_start;

    /* Copia las posiciones iniciales de los cuatro fantasmas. */
    for (int i = 0; i < NUM_GHOSTS; ++i) {
        state->ghost_start[i] = map->ghost_start[i];
    }

    /* Copia la grilla para que P1 y P2 puedan leerla desde memoria compartida. */
    for (int row = 0; row < map->rows; ++row) {
        memcpy(state->map_grid[row], map->grid[row], (size_t)map->cols + 1);
    }
}

static process_id_t choose_next_process(shared_state_t *state,
                                        process_id_t *last_round_robin) {
    /* Bloquea las prioridades porque son datos compartidos. */
    pthread_mutex_lock(&state->priority_mutex);

    /* Copia las prioridades a variables locales para decidir sin sostener el mutex. */
    int pacman_priority = state->prioridad_pacman;
    int enemy_priority = state->prioridad_enemy;

    /* Libera el mutex apenas termina de leer los datos protegidos. */
    pthread_mutex_unlock(&state->priority_mutex);

    /* Si Pac-Man tiene mas prioridad, P0 le da el turno a P1. */
    if (pacman_priority > enemy_priority) {
        return PROCESS_PACMAN;
    }

    /* Si los enemigos tienen mas prioridad, P0 le da el turno a P2. */
    if (enemy_priority > pacman_priority) {
        return PROCESS_ENEMY;
    }

    /* Si hay empate, Round Robin alterna respecto del ultimo proceso elegido. */
    if (*last_round_robin == PROCESS_PACMAN) {
        *last_round_robin = PROCESS_ENEMY;
        return PROCESS_ENEMY;
    }

    /* Si el ultimo fue P2, ahora toca P1. */
    *last_round_robin = PROCESS_PACMAN;
    return PROCESS_PACMAN;
}

static int run_scheduler_ticks(shared_state_t *state) {
    /* Arrancamos como si el ultimo turno hubiera sido de enemigos para que P1 inicie. */
    process_id_t last_round_robin = PROCESS_ENEMY;

    /* P0 ejecuta una cantidad limitada de ticks para este avance demostrativo. */
    for (int tick = 1; tick <= state->max_ticks; ++tick) {
        /* Protege global_tick porque P1 y P2 tambien pueden leerlo. */
        pthread_mutex_lock(&state->state_mutex);

        /* Actualiza el tick global visible para todos los procesos. */
        state->global_tick = tick;

        /* Libera el mutex luego de escribir el tick. */
        pthread_mutex_unlock(&state->state_mutex);

        /* Decide si el turno sera de P1 o P2 segun prioridades y Round Robin. */
        process_id_t selected = choose_next_process(state, &last_round_robin);

        /* Muestra en consola la decision del scheduler. */
        printf("[P0] Tick %d: turno para %s\n", tick, process_name(selected));

        /* Si gano P1, P0 libera el semaforo de Pac-Man. */
        if (selected == PROCESS_PACMAN) {
            if (sem_post(&state->sem_pacman_turn) != 0) {
                perror("[P0] sem_post sem_pacman_turn");
                return -1;
            }
        } else {
            /* Si gano P2, P0 libera el semaforo de enemigos. */
            if (sem_post(&state->sem_enemy_turn) != 0) {
                perror("[P0] sem_post sem_enemy_turn");
                return -1;
            }
        }

        /* P0 espera hasta que P1 o P2 avise que termino su turno. */
        if (sem_wait(&state->sem_turn_done) != 0) {
            perror("[P0] sem_wait sem_turn_done");
            return -1;
        }
    }

    return 0;
}

static void request_processes_to_finish(shared_state_t *state) {
    /* Protege game_over porque los procesos hijos lo leeran desde sus threads. */
    pthread_mutex_lock(&state->state_mutex);

    /* Marca el final del juego para que P1 y P2 salgan de sus bucles. */
    state->game_over = 1;

    /* Libera el mutex luego de cambiar el estado global. */
    pthread_mutex_unlock(&state->state_mutex);

    /* Despierta a P1 por si estaba bloqueado esperando su turno. */
    sem_post(&state->sem_pacman_turn);

    /* Despierta a P2 por si estaba bloqueado esperando su turno. */
    sem_post(&state->sem_enemy_turn);
}

static int wait_for_child(pid_t pid, const char *label) {
    /* status guardara la forma en que termino el proceso hijo. */
    int status = 0;

    /* waitpid evita que el proceso hijo quede como zombie. */
    if (waitpid(pid, &status, 0) == -1) {
        perror("[P0] waitpid");
        return -1;
    }

    /* WIFEXITED confirma que el hijo termino con exit o _exit. */
    if (WIFEXITED(status)) {
        printf("[P0] %s termino con codigo %d\n", label, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }

    /* Si no termino normalmente, P0 lo reporta como error. */
    printf("[P0] %s termino de forma no normal\n", label);
    return -1;
}

int scheduler_process_main(const char *case_dir) {
    /* map_path guardara la ruta completa hacia map.txt. */
    char map_path[MAX_PATH_LENGTH];

    /* error guardara mensajes generados por el lector del mapa. */
    char error[MAX_ERROR_LENGTH];

    /* map es una estructura local usada solo mientras P0 carga el mapa. */
    game_map_t map;

    /* state apuntara a memoria compartida visible para P0, P1 y P2. */
    shared_state_t *state;

    /* pacman_pid guardara el PID del proceso hijo P1. */
    pid_t pacman_pid;

    /* enemy_pid guardara el PID del proceso hijo P2. */
    pid_t enemy_pid;

    /* result acumula si ocurrio algun error durante la ejecucion. */
    int result = 0;

    /* Desactiva buffer de stdout para ver mensajes ordenados entre procesos. */
    setbuf(stdout, NULL);

    /* Mensaje inicial de P0. */
    printf("[P0] %s inicializando arquitectura base\n",
           process_name(PROCESS_SCHEDULER));

    /* Construye la ruta al archivo map.txt del caso elegido. */
    build_map_path(case_dir, map_path, sizeof(map_path));

    /* Informa que archivo intentara leer. */
    printf("[P0] Leyendo mapa: %s\n", map_path);

    /* Carga y valida el mapa antes de crear procesos hijos. */
    if (map_load(map_path, &map, error, sizeof(error)) != 0) {
        fprintf(stderr, "[P0] Error al cargar mapa: %s\n", error);
        return 1;
    }

    /* Imprime resumen de dimensiones y posiciones iniciales. */
    map_print_summary(&map);

    /* Reserva memoria compartida con mmap para el estado global. */
    state = shared_state_create();

    /* Si mmap falla, no hay estado comun y el programa termina. */
    if (state == NULL) {
        return 1;
    }

    /* Copia mapa, posiciones, vidas y prioridades al estado compartido. */
    shared_state_init_from_map(state, &map);

    /* Inicializa semaforos y mutex aptos para procesos. */
    if (shared_state_init_synchronization(state) != 0) {
        shared_state_release(state);
        return 1;
    }

    /* Reporta que el estado compartido ya esta listo. */
    printf("[P0] Estado compartido base inicializado\n");

    /* Reporta prioridades iniciales para explicar la decision del scheduler. */
    printf("[P0] Prioridades iniciales: Pac-Man=%d, Enemigos=%d\n",
           state->prioridad_pacman,
           state->prioridad_enemy);

    /* Muestra la parte logica inicial de P1 antes de crear el proceso real. */
    pacman_process_bootstrap(state);

    /* Muestra la parte logica inicial de P2 antes de crear el proceso real. */
    enemy_process_bootstrap(state);

    /* fork crea el proceso hijo que ejecutara P1. */
    pacman_pid = fork();

    /* Un PID negativo significa que fork fallo. */
    if (pacman_pid < 0) {
        perror("[P0] fork P1");
        shared_state_destroy_synchronization(state);
        shared_state_release(state);
        return 1;
    }

    /* En el hijo, fork devuelve 0, asi que aqui corre P1. */
    if (pacman_pid == 0) {
        _exit(pacman_process_main(state, case_dir));
    }

    /* Solo el padre llega aqui y crea el proceso hijo P2. */
    enemy_pid = fork();

    /* Si falla fork de P2, P0 pide terminar a P1 y limpia recursos. */
    if (enemy_pid < 0) {
        perror("[P0] fork P2");
        request_processes_to_finish(state);
        wait_for_child(pacman_pid, "P1");
        shared_state_destroy_synchronization(state);
        shared_state_release(state);
        return 1;
    }

    /* En el segundo hijo, fork devuelve 0, asi que aqui corre P2. */
    if (enemy_pid == 0) {
        _exit(enemy_process_main(state, case_dir));
    }

    /* El padre P0 conserva los PID reales para esperar a sus hijos. */
    printf("[P0] Procesos creados: P1 PID=%ld, P2 PID=%ld\n",
           (long)pacman_pid,
           (long)enemy_pid);

    /* Ejecuta el scheduler por ticks y entrega turnos con semaforos. */
    if (run_scheduler_ticks(state) != 0) {
        result = 1;
    }

    /* Al terminar los ticks, P0 activa game_over y despierta hijos bloqueados. */
    request_processes_to_finish(state);

    /* Espera a P1 para evitar proceso zombie. */
    if (wait_for_child(pacman_pid, "P1") != 0) {
        result = 1;
    }

    /* Espera a P2 para evitar proceso zombie. */
    if (wait_for_child(enemy_pid, "P2") != 0) {
        result = 1;
    }

    /* Lee el resumen final protegido por mutex. */
    pthread_mutex_lock(&state->state_mutex);

    /* Imprime cuantos turnos ejecuto cada proceso hijo. */
    printf("[P0] Resumen: turnos Pac-Man=%d, turnos fantasmas=%d\n",
           state->pacman_turns_executed,
           state->ghost_turns_executed);

    /* Libera el mutex despues de leer el resumen. */
    pthread_mutex_unlock(&state->state_mutex);

    /* Destruye semaforos y mutex antes de liberar la memoria compartida. */
    shared_state_destroy_synchronization(state);

    /* Libera el bloque creado con mmap. */
    shared_state_release(state);

    /* Mensaje final del avance. */
    printf("[P0] Avance POSIX completado: fork, threads, semaforos y memoria compartida funcionando\n");

    /* Devuelve 0 si todo salio bien o 1 si algo fallo. */
    return result;
}
