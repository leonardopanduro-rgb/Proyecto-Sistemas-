#include "movement_queue.h"

void queue_init(movement_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    sem_init(&q->items_available, 0, 0); // Semáforo local (pshared = 0)
}

void queue_push(movement_queue_t *q, movement_cmd_t cmd) {
    pthread_mutex_lock(&q->mutex);
    if (q->count < MAX_QUEUE_SIZE) {
        q->buffer[q->tail] = cmd;
        q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
        q->count++;
        sem_post(&q->items_available); // Avisa que hay un nuevo item
    }
    pthread_mutex_unlock(&q->mutex);
}

movement_cmd_t queue_pop(movement_queue_t *q) {
    movement_cmd_t cmd = {CMD_NONE, 0};
    // Espera a que haya items (Productor-Consumidor)
    if (sem_wait(&q->items_available) == 0) { 
        pthread_mutex_lock(&q->mutex);
        cmd = q->buffer[q->head];
        q->head = (q->head + 1) % MAX_QUEUE_SIZE;
        q->count--;
        pthread_mutex_unlock(&q->mutex);
    }
    return cmd;
}

void queue_reset(movement_queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    q->head = 0;
    q->count = q->tail; // q->tail guarda el total de elementos insertados
    
    // Drenamos el semáforo actual por si quedaron permisos sueltos
    while (sem_trywait(&q->items_available) == 0);
    
    // Restauramos los permisos del semáforo al total original
    for (int i = 0; i < q->tail; i++) {
        sem_post(&q->items_available);
    }
    pthread_mutex_unlock(&q->mutex);
}