#ifndef MOVEMENT_QUEUE_H
#define MOVEMENT_QUEUE_H

#include <pthread.h>
#include <semaphore.h>

#define MAX_QUEUE_SIZE 256

typedef enum {
    CMD_UP, CMD_DOWN, CMD_LEFT, CMD_RIGHT, CMD_SET_PRIORITY, CMD_NONE
} command_type_t;

typedef struct {
    command_type_t type;
    int value; // Usado solo para SET_PRIORITY
} movement_cmd_t;

typedef struct {
    movement_cmd_t buffer[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    sem_t items_available;
} movement_queue_t;

void queue_init(movement_queue_t *q);
void queue_push(movement_queue_t *q, movement_cmd_t cmd);
movement_cmd_t queue_pop(movement_queue_t *q);
void queue_reset(movement_queue_t *q);
#endif