#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p2_threads.h"
#include "ghost.h"

void inicializar_enemy_thread_data(
    EnemyThreadData *data,
    SharedData *shared,
    const char *carpeta_caso
) {
    data->shared = shared;
    data->terminar = 0;

    inicializar_fantasmas_desde_shared(shared, data->ghosts);

    construir_ruta(data->rutas_ghost[0], sizeof(data->rutas_ghost[0]),
                   carpeta_caso, "ghost_1_moves.txt");

    construir_ruta(data->rutas_ghost[1], sizeof(data->rutas_ghost[1]),
                   carpeta_caso, "ghost_2_moves.txt");

    construir_ruta(data->rutas_ghost[2], sizeof(data->rutas_ghost[2]),
                   carpeta_caso, "ghost_3_moves.txt");

    construir_ruta(data->rutas_ghost[3], sizeof(data->rutas_ghost[3]),
                   carpeta_caso, "ghost_4_moves.txt");

    data->pacman_last_y = shared->pacman_y;
    data->pacman_last_x = shared->pacman_x;

    data->pacman_previous_y = shared->pacman_y;
    data->pacman_previous_x = shared->pacman_x;

    for (int i = 0; i < NUM_GHOSTS; i++) {
        data->ghost_previous_y[i] = data->ghosts[i].y;
        data->ghost_previous_x[i] = data->ghosts[i].x;
    }

    pthread_mutex_init(&data->mutex_ghosts, NULL);
    pthread_mutex_init(&data->mutex_pacman_local, NULL);
    pthread_mutex_init(&data->mutex_terminar, NULL);

    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_init(&data->sem_ghost_turn[i], 0, 0);
        sem_init(&data->sem_ghost_done[i], 0, 0);
    }

    sem_init(&data->sem_tracker_start, 0, 0);
    sem_init(&data->sem_tracker_done, 0, 0);

    sem_init(&data->sem_collision_start, 0, 0);
    sem_init(&data->sem_collision_done, 0, 0);
}

/*
    Libera recursos internos de P2.
*/
/*
    Punto 13.
    El acceso a data->terminar de P2 se centraliza bajo mutex_terminar.
*/
int enemy_debe_terminar(EnemyThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_terminar);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_terminar);

    return valor;
}

void marcar_enemy_terminar(EnemyThreadData *data) {
    pthread_mutex_lock(&data->mutex_terminar);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_terminar);
}

void destruir_enemy_thread_data(EnemyThreadData *data) {
    pthread_mutex_destroy(&data->mutex_ghosts);
    pthread_mutex_destroy(&data->mutex_pacman_local);
    pthread_mutex_destroy(&data->mutex_terminar);

    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_destroy(&data->sem_ghost_turn[i]);
        sem_destroy(&data->sem_ghost_done[i]);
    }

    sem_destroy(&data->sem_tracker_start);
    sem_destroy(&data->sem_tracker_done);

    sem_destroy(&data->sem_collision_start);
    sem_destroy(&data->sem_collision_done);
}

/*
    Hilo auxiliar general para fantasmas.

    Los wrappers ghost_thread_1, ghost_thread_2, etc.
    llaman a esta función.
*/
void *ghost_thread_generico(void *arg) {
    GhostThreadArg *ghost_arg = (GhostThreadArg *)arg;
    EnemyThreadData *data = ghost_arg->data;
    SharedData *shared = data->shared;
    int id = ghost_arg->ghost_index;

    FILE *archivo = fopen(data->rutas_ghost[id], "r");

    if (archivo == NULL) {
        printf("[P2-ghost-%d] No se pudo abrir %s\n",
               id + 1,
               data->rutas_ghost[id]);

        publicar_error_entrada(shared, 2);
    }

    printf("[P2-ghost-%d] ghost_thread_%d iniciado\n",
           id + 1,
           id + 1);

    while (1) {
        sem_wait(&data->sem_ghost_turn[id]);

        if (enemy_debe_terminar(data) || obtener_game_over(shared)) {
            break;
        }

        char movimiento[MAX_MOVE];
        int estado_lectura = leer_movimiento(archivo,
                                             movimiento,
                                             sizeof(movimiento));

        if (estado_lectura == LECTURA_OK) {
            int nueva_prioridad;

            if (extraer_prioridad(movimiento, &nueva_prioridad)) {
                solicitar_prioridad_enemy(shared, nueva_prioridad);
            } else {
                pthread_mutex_lock(&data->mutex_ghosts);

                mover_fantasma(shared, &data->ghosts[id], movimiento);

                pthread_mutex_unlock(&data->mutex_ghosts);
            }
        } else if (estado_lectura == LECTURA_INVALIDA ||
                   estado_lectura == LECTURA_ERROR) {
            publicar_error_entrada(shared, 2);

            printf("[P2-ghost-%d] Error en archivo de movimientos\n",
                   id + 1);
        } else {
            printf("[P2-ghost-%d] Fantasma %c no tiene más movimientos\n",
                   id + 1,
                   data->ghosts[id].simbolo);
            publicar_fantasma_agotado(shared, id);
        }

        sem_post(&data->sem_ghost_done[id]);
    }

    if (archivo != NULL) {
        fclose(archivo);
    }

    printf("[P2-ghost-%d] ghost_thread_%d finalizado\n",
           id + 1,
           id + 1);

    return NULL;
}

/*
    Wrappers con los nombres pedidos por la profesora.
*/
void *ghost_thread_1(void *arg) {
    return ghost_thread_generico(arg);
}

void *ghost_thread_2(void *arg) {
    return ghost_thread_generico(arg);
}

void *ghost_thread_3(void *arg) {
    return ghost_thread_generico(arg);
}

void *ghost_thread_4(void *arg) {
    return ghost_thread_generico(arg);
}

/*
    Hilo pacman_tracker_thread.

    Lee la posición actual de Pac-Man desde memoria compartida
    y guarda una copia local dentro de P2.
*/
void *pacman_tracker_thread(void *arg) {
    EnemyThreadData *data = (EnemyThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P2-tracker] pacman_tracker_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_tracker_start);

        if (enemy_debe_terminar(data) || obtener_game_over(shared)) {
            break;
        }

        pthread_mutex_lock(&shared->mutex_shared);

        int y = shared->pacman_y;
        int x = shared->pacman_x;

        pthread_mutex_unlock(&shared->mutex_shared);

        pthread_mutex_lock(&data->mutex_pacman_local);

        /*
            Antes de sobrescribir, conservamos la posición anterior para
            poder detectar el cruce con un fantasma.
        */
        data->pacman_previous_y = data->pacman_last_y;
        data->pacman_previous_x = data->pacman_last_x;

        data->pacman_last_y = y;
        data->pacman_last_x = x;

        pthread_mutex_unlock(&data->mutex_pacman_local);

        printf("[P2-tracker] Copia local de Pac-Man: (%d,%d)\n", y, x);

        sem_post(&data->sem_tracker_done);
    }

    printf("[P2-tracker] pacman_tracker_thread finalizado\n");

    return NULL;
}

/*
    Hilo collision_thread.

    Compara la copia local de Pac-Man con las posiciones internas
    de los fantasmas.
*/
void *collision_thread(void *arg) {
    EnemyThreadData *data = (EnemyThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P2-collision] collision_thread iniciado\n");

    while (1) {
        sem_wait(&data->sem_collision_start);

        if (enemy_debe_terminar(data) || obtener_game_over(shared)) {
            break;
        }

        pthread_mutex_lock(&data->mutex_pacman_local);

        int pacman_y = data->pacman_last_y;
        int pacman_x = data->pacman_last_x;
        int pacman_prev_y = data->pacman_previous_y;
        int pacman_prev_x = data->pacman_previous_x;

        pthread_mutex_unlock(&data->mutex_pacman_local);

        int colision = 0;
        int ghost_id = -1;
        char ghost_simbolo = '?';
        const char *motivo = "";

        /*
            BONUS P3: capturamos la posicion ACTUAL de los cuatro fantasmas
            para publicarla luego en memoria compartida. Se lee aqui, dentro
            de mutex_ghosts, para tener una foto consistente.
        */
        int ghost_pub_y[NUM_GHOSTS];
        int ghost_pub_x[NUM_GHOSTS];

        pthread_mutex_lock(&data->mutex_ghosts);

        for (int i = 0; i < NUM_GHOSTS; i++) {
            ghost_pub_y[i] = data->ghosts[i].y;
            ghost_pub_x[i] = data->ghosts[i].x;
        }

        for (int i = 0; i < NUM_GHOSTS; i++) {
            int gy = data->ghosts[i].y;
            int gx = data->ghosts[i].x;
            int gpy = data->ghost_previous_y[i];
            int gpx = data->ghost_previous_x[i];

            /*
                Cruce: Pac-Man y el fantasma intercambian posiciones.
                Pac-Man actual == posición anterior del fantasma y
                Pac-Man anterior == posición actual del fantasma.
            */
            if (pacman_y == gpy && pacman_x == gpx &&
                pacman_prev_y == gy && pacman_prev_x == gx) {
                colision = 1;
                motivo = "cruce";
            }
            /*
                Choque: el fantasma terminó en la celda de Pac-Man.
            */
            else if (pacman_y == gy && pacman_x == gx) {
                colision = 1;
                motivo = "choque";
            }
            if (colision == 1) {
                ghost_id = data->ghosts[i].id;
                ghost_simbolo = data->ghosts[i].simbolo;
                break;
            }
        }

        pthread_mutex_unlock(&data->mutex_ghosts);

        pthread_mutex_lock(&shared->mutex_shared);

        /*
            BONUS P3: publicar el estado visible de los enemigos (posicion
            actual) bajo el MISMO mutex_shared, en cada turno de P2.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            shared->ghost_y[i] = ghost_pub_y[i];
            shared->ghost_x[i] = ghost_pub_x[i];
        }

        if (colision == 1) {
            printf("\n[P2-collision] COLISIÓN (%s) con fantasma %c\n",
                   motivo,
                   ghost_simbolo);

            shared->collision_detected = 1;
            shared->collision_tick = shared->global_tick;
            shared->collision_ghost_id = ghost_id;
        } else {
            shared->collision_detected = 0;
            shared->collision_tick = -1;
            shared->collision_ghost_id = -1;
        }

        pthread_mutex_unlock(&shared->mutex_shared);

        sem_post(&data->sem_collision_done);
    }

    printf("[P2-collision] collision_thread finalizado\n");

    return NULL;
}

/*
    Hilo enemy_controller.

    Este hilo espera el permiso de P0.
    Cuando P0 le da turno a P2:
    1. Activa pacman_tracker_thread.
    2. Activa los 4 ghost_thread.
    3. Espera a que todos terminen.
    4. Activa collision_thread.
    5. Avisa a P0 con sem_turn_done.
*/
void *enemy_controller(void *arg) {
    EnemyThreadData *data = (EnemyThreadData *)arg;
    SharedData *shared = data->shared;

    printf("[P2-controller] enemy_controller iniciado\n");

    while (1) {
        sem_wait(&shared->sem_enemy_turn);

        if (obtener_game_over(shared)) {
            marcar_enemy_terminar(data);
            break;
        }

        printf("[P2-controller] Turno recibido en tick %d\n",
               obtener_global_tick(shared));

        /*
            1. Actualizar copia local de Pac-Man (posición actual y anterior).
        */
        sem_post(&data->sem_tracker_start);
        sem_wait(&data->sem_tracker_done);

        /*
            2. Guardar la posición anterior de cada fantasma antes de moverlo.
               El collision_thread la usará para detectar el cruce.
        */
        pthread_mutex_lock(&data->mutex_ghosts);
        for (int i = 0; i < NUM_GHOSTS; i++) {
            data->ghost_previous_y[i] = data->ghosts[i].y;
            data->ghost_previous_x[i] = data->ghosts[i].x;
        }
        pthread_mutex_unlock(&data->mutex_ghosts);

        /*
            3. Despertar a los 4 fantasmas.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            sem_post(&data->sem_ghost_turn[i]);
        }

        /*
            4. Esperar a que los 4 fantasmas terminen.
        */
        for (int i = 0; i < NUM_GHOSTS; i++) {
            sem_wait(&data->sem_ghost_done[i]);
        }

        /*
            5. Revisar choque y cruce después de que todos se movieron.
        */
        sem_post(&data->sem_collision_start);
        sem_wait(&data->sem_collision_done);

        printf("[P2-controller] Fin de turno\n");

        /*
            6. Avisar a P0 que P2 terminó.
        */
        sem_post(&shared->sem_turn_done);
    }

    /*
        Liberamos todos los hilos que podrían estar bloqueados.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        sem_post(&data->sem_ghost_turn[i]);
    }

    sem_post(&data->sem_tracker_start);
    sem_post(&data->sem_collision_start);

    printf("[P2-controller] enemy_controller finalizado\n");

    return NULL;
}
