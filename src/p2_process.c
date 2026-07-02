#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p2_threads.h"
/*
 * Punto de entrada del proceso P2.
 *
 * Crea controller, cuatro fantasmas, tracker y collision. Los argumentos de
 * cada ghost incluyen un índice estable dentro del arreglo local. Al terminar
 * controller, marca la bandera sincronizada y publica los semáforos de inicio
 * para liberar trabajadores dormidos. Solo destruye recursos tras los siete
 * pthread_join(), evitando carreras con objetos POSIX ya destruidos.
 */
void enemy_process(SharedData *shared, const char *carpeta_caso) {
    printf("[P2] enemy_process iniciado\n");
    printf("[P2] PID=%d | PPID=%d\n", getpid(), getppid());

    EnemyThreadData data;
    inicializar_enemy_thread_data(&data, shared, carpeta_caso);

    GhostThreadArg ghost_args[NUM_GHOSTS];

    for (int i = 0; i < NUM_GHOSTS; i++) {
        ghost_args[i].data = &data;
        ghost_args[i].ghost_index = i;
    }

    pthread_t hilo_controller;
    pthread_t hilo_ghost_1;
    pthread_t hilo_ghost_2;
    pthread_t hilo_ghost_3;
    pthread_t hilo_ghost_4;
    pthread_t hilo_tracker;
    pthread_t hilo_collision;

    pthread_create(&hilo_controller, NULL, enemy_controller, &data);

    pthread_create(&hilo_ghost_1, NULL, ghost_thread_1, &ghost_args[0]);
    pthread_create(&hilo_ghost_2, NULL, ghost_thread_2, &ghost_args[1]);
    pthread_create(&hilo_ghost_3, NULL, ghost_thread_3, &ghost_args[2]);
    pthread_create(&hilo_ghost_4, NULL, ghost_thread_4, &ghost_args[3]);

    pthread_create(&hilo_tracker, NULL, pacman_tracker_thread, &data);
    pthread_create(&hilo_collision, NULL, collision_thread, &data);

    pthread_join(hilo_controller, NULL);

    marcar_enemy_terminar(&data);

    /* Despertar cada posible sem_wait para que observe terminar=1 y salga. */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_post(&data.sem_ghost_turn[i]);
    }

    sem_post(&data.sem_tracker_start);
    sem_post(&data.sem_collision_start);

    pthread_join(hilo_ghost_1, NULL);
    pthread_join(hilo_ghost_2, NULL);
    pthread_join(hilo_ghost_3, NULL);
    pthread_join(hilo_ghost_4, NULL);

    pthread_join(hilo_tracker, NULL);
    pthread_join(hilo_collision, NULL);

    destruir_enemy_thread_data(&data);

    printf("[P2] enemy_process finalizado\n");
    exit(0);
}
