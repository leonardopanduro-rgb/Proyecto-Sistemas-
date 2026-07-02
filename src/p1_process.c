#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p1_threads.h"

/*
    P1 = pacman_process

    Ahora P1 crea 3 hilos internos.
*/
void pacman_process(SharedData *shared, const char *carpeta_caso) {
    printf("[P1] pacman_process iniciado\n");
    printf("[P1] PID=%d | PPID=%d\n", getpid(), getppid());

    char ruta_pacman[256];
    construir_ruta(ruta_pacman, sizeof(ruta_pacman), carpeta_caso, "pacman_moves.txt");

    PacmanThreadData data;
    inicializar_pacman_thread_data(&data, shared, ruta_pacman);

    pthread_t hilo_reader;
    pthread_t hilo_executor;
    pthread_t hilo_publisher;

    pthread_create(&hilo_reader, NULL, movement_reader_thread, &data);
    pthread_create(&hilo_executor, NULL, movement_executor_thread, &data);
    pthread_create(&hilo_publisher, NULL, pacman_publisher_thread, &data);

    pthread_join(hilo_executor, NULL);

    marcar_pacman_terminar(&data);

    /*
        Liberamos al reader por si estuviera esperando espacio.
    */
    for (int i = 0; i < P1_QUEUE_SIZE; i++) {
        sem_post(&data.sem_hay_espacio);
    }

    sem_post(&data.sem_hay_movimientos);
    sem_post(&data.sem_estado_pacman_listo);

    pthread_join(hilo_reader, NULL);
    pthread_join(hilo_publisher, NULL);

    destruir_pacman_thread_data(&data);

    printf("[P1] pacman_process finalizado\n");
    exit(0);
}
