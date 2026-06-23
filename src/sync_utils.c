#include "sync_utils.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
/* Algunos sistemas antiguos usan MAP_ANON en lugar de MAP_ANONYMOUS. */
#define MAP_ANONYMOUS MAP_ANON
#endif

static void print_pthread_error(const char *label, int error_code) {
    /* strerror convierte el codigo pthread en texto entendible. */
    fprintf(stderr, "%s: %s\n", label, strerror(error_code));
}

static int init_process_shared_mutex(pthread_mutex_t *mutex) {
    /* attr permite configurar como se comportara el mutex. */
    pthread_mutexattr_t attr;
    int error_code;

    /* Inicializa la estructura de atributos del mutex. */
    error_code = pthread_mutexattr_init(&attr);

    /* Si falla, se imprime el error devuelto por pthread. */
    if (error_code != 0) {
        print_pthread_error("pthread_mutexattr_init", error_code);
        return -1;
    }

    /* PTHREAD_PROCESS_SHARED permite que el mutex funcione entre procesos. */
    error_code = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    /* Si falla, se destruye attr antes de salir. */
    if (error_code != 0) {
        print_pthread_error("pthread_mutexattr_setpshared", error_code);
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    /* Inicializa el mutex usando los atributos interproceso. */
    error_code = pthread_mutex_init(mutex, &attr);

    /* Si falla, se destruye attr antes de salir. */
    if (error_code != 0) {
        print_pthread_error("pthread_mutex_init", error_code);
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    /* Destruye los atributos porque el mutex ya quedo inicializado. */
    pthread_mutexattr_destroy(&attr);

    /* Retorna 0 para indicar exito. */
    return 0;
}

shared_state_t *shared_state_create(void) {
    /* mmap reserva una zona de memoria visible para padre e hijos despues de fork. */
    shared_state_t *state = mmap(NULL,
                                 sizeof(shared_state_t),
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS,
                                 -1,
                                 0);

    /* MAP_FAILED indica que no se pudo crear la memoria compartida anonima. */
    if (state == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    /* Limpia la memoria para que todos los campos empiecen en cero. */
    memset(state, 0, sizeof(*state));

    /* Devuelve el puntero compartido que usaran P0, P1 y P2. */
    return state;
}

int shared_state_init_synchronization(shared_state_t *state) {
    /* sem_pacman_turn empieza en 0 para que P1 espere hasta que P0 lo libere. */
    if (sem_init(&state->sem_pacman_turn, 1, 0) != 0) {
        perror("sem_init sem_pacman_turn");
        return -1;
    }

    /* sem_enemy_turn empieza en 0 para que P2 espere hasta que P0 lo libere. */
    if (sem_init(&state->sem_enemy_turn, 1, 0) != 0) {
        perror("sem_init sem_enemy_turn");
        sem_destroy(&state->sem_pacman_turn);
        return -1;
    }

    /* sem_turn_done permite que P1 o P2 avisen a P0 que terminaron su turno. */
    if (sem_init(&state->sem_turn_done, 1, 0) != 0) {
        perror("sem_init sem_turn_done");
        sem_destroy(&state->sem_enemy_turn);
        sem_destroy(&state->sem_pacman_turn);
        return -1;
    }

    /* state_mutex protege datos generales como tick, game_over y posiciones. */
    if (init_process_shared_mutex(&state->state_mutex) != 0) {
        sem_destroy(&state->sem_turn_done);
        sem_destroy(&state->sem_enemy_turn);
        sem_destroy(&state->sem_pacman_turn);
        return -1;
    }

    /* priority_mutex protege prioridad_pacman y prioridad_enemy. */
    if (init_process_shared_mutex(&state->priority_mutex) != 0) {
        pthread_mutex_destroy(&state->state_mutex);
        sem_destroy(&state->sem_turn_done);
        sem_destroy(&state->sem_enemy_turn);
        sem_destroy(&state->sem_pacman_turn);
        return -1;
    }

    /* collision_mutex queda listo para el siguiente avance de colisiones. */
    if (init_process_shared_mutex(&state->collision_mutex) != 0) {
        pthread_mutex_destroy(&state->priority_mutex);
        pthread_mutex_destroy(&state->state_mutex);
        sem_destroy(&state->sem_turn_done);
        sem_destroy(&state->sem_enemy_turn);
        sem_destroy(&state->sem_pacman_turn);
        return -1;
    }

    /* Retorna 0 si todos los semaforos y mutex se inicializaron bien. */
    return 0;
}

void shared_state_destroy_synchronization(shared_state_t *state) {
    /* Destruye el mutex de colisiones. */
    pthread_mutex_destroy(&state->collision_mutex);

    /* Destruye el mutex de prioridades. */
    pthread_mutex_destroy(&state->priority_mutex);

    /* Destruye el mutex del estado general. */
    pthread_mutex_destroy(&state->state_mutex);

    /* Destruye el semaforo usado para confirmar fin de turno. */
    sem_destroy(&state->sem_turn_done);

    /* Destruye el semaforo de turno de enemigos. */
    sem_destroy(&state->sem_enemy_turn);

    /* Destruye el semaforo de turno de Pac-Man. */
    sem_destroy(&state->sem_pacman_turn);
}

void shared_state_release(shared_state_t *state) {
    /* Evita llamar munmap con un puntero nulo. */
    if (state != NULL) {
        /* Libera la memoria compartida reservada con mmap. */
        munmap(state, sizeof(*state));
    }
}
