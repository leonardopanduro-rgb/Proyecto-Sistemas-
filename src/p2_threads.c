#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p2_threads.h"
#include "ghost.h"

/*
 * Coordinación interna del proceso P2.
 *
 * enemy_controller dirige una fase ordenada por cada turno de P0: tracker,
 * cuatro fantasmas en paralelo, detector de colisión y confirmación a P0.
 * mutex_ghosts protege posiciones actuales/anteriores; mutex_pacman_local
 * protege la copia local de Pac-Man; mutex_terminar elimina la race sobre la
 * bandera de cierre. Cada pareja sem_*_start/done actúa como una barrera de
 * fase y evita que collision_thread lea posiciones mientras aún cambian.
 */

/*
 * Inicializa datos y sincronización privada de P2.
 * data permanece válido hasta unir los siete hilos; shared es el mmap común y
 * carpeta_caso permite construir un archivo distinto para cada fantasma. Los
 * semáforos usan pshared=0 porque solo coordinan hilos del mismo proceso.
 */
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

/* Lee terminar bajo mutex_terminar para evitar carreras durante el cierre. */
int enemy_debe_terminar(EnemyThreadData *data) {
    int valor;

    pthread_mutex_lock(&data->mutex_terminar);
    valor = data->terminar;
    pthread_mutex_unlock(&data->mutex_terminar);

    return valor;
}

/* Escribe terminar=1 bajo el mismo mutex que usan todas las lecturas. */
void marcar_enemy_terminar(EnemyThreadData *data) {
    pthread_mutex_lock(&data->mutex_terminar);
    data->terminar = 1;
    pthread_mutex_unlock(&data->mutex_terminar);
}

/* Destruye mutex y semáforos privados solo después de todos los joins. */
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
 * Implementación común de los cuatro hilos fantasma.
 *
 * arg contiene EnemyThreadData y el índice 0..3. Cada hilo abre su propio
 * archivo y espera sem_ghost_turn[id], por lo que no actúa sin que controller
 * inicie la fase. mutex_ghosts serializa los cambios al arreglo compartido
 * dentro de P2. Al terminar su única acción publica sem_ghost_done[id].
 * Retorna NULL como exige pthread_create().
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

/* Wrappers que conservan los cuatro nombres exigidos y delegan la lógica. */
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
 * Hilo de seguimiento de Pac-Man.
 *
 * Lee pacman_x/y juntos bajo mutex_shared y luego actualiza la copia actual y
 * anterior bajo mutex_pacman_local. Los mutex pertenecen a dominios distintos
 * y nunca se mantienen simultáneamente, reduciendo riesgo de deadlock. El par
 * tracker_start/tracker_done impide que controller avance con datos antiguos.
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

        /* Conservar ambos estados permite detectar intercambio de celdas. */
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
 * Hilo detector y publicador de colisiones.
 *
 * Se activa después de los cuatro ghost_done. Toma instantáneas protegidas de
 * Pac-Man y fantasmas, detecta misma celda o intercambio, y publica posiciones
 * y evento bajo mutex_shared. P2 no descuenta vidas: P0 consume el evento una
 * sola vez. sem_collision_done garantiza que controller no confirme el turno
 * antes de completar esa publicación.
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

        /* Instantánea consistente: ningún ghost puede modificarla a la vez. */
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

            /* Cruce: ambas entidades intercambiaron sus celdas en el tick. */
            if (pacman_y == gpy && pacman_x == gpx &&
                pacman_prev_y == gy && pacman_prev_x == gx) {
                colision = 1;
                motivo = "cruce";
            }
            /* Choque directo: ambos terminaron en la misma celda. */
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
         * SECCIÓN CRÍTICA INTERPROCESO: posiciones y evento se publican como
         * una unidad; renderer y P0 nunca observan un frame parcialmente nuevo.
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
 * Hilo controlador de P2.
 *
 * Espera sem_enemy_turn, única autorización emitida por P0. Ejecuta fases:
 * (1) tracker y espera done; (2) guarda posiciones anteriores bajo mutex;
 * (3) despierta cuatro fantasmas; (4) espera sus cuatro done; (5) ejecuta y
 * espera collision; (6) publica sem_turn_done. Estas barreras permiten mover
 * fantasmas en paralelo sin que detección, publicación o siguiente tick se
 * adelanten. En cierre despierta trabajadores para que alcancen sus joins.
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
