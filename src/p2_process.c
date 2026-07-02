#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p2_threads.h"



/*
    P2 = enemy_process

    En este checkpoint P2 todavía se mantiene igual.
    Sus hilos internos vienen en el siguiente checkpoint.
*/
/*
    P2 = enemy_process

    Ahora P2 crea hilos internos:
    - enemy_controller
    - ghost_thread_1
    - ghost_thread_2
    - ghost_thread_3
    - ghost_thread_4
    - pacman_tracker_thread
    - collision_thread
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

    /*
        Liberamos por seguridad los hilos internos.
    */
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
