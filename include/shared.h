#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <semaphore.h>

#define MAX_Y 50
#define MAX_X 50
#define NUM_GHOSTS 4
#define MAX_MOVE 64

typedef struct {
    /* Control global: P0 escribe; los demás procesos consultan bajo mutex. */
    int global_tick;
    int max_ticks;
    int game_over;

    /* Geometría y mapa inmutable una vez que P0 termina cargar_mapa(). */
    int filas;
    int columnas;

    char map_grid[MAX_Y][MAX_X];

    /* Estado de Pac-Man publicado por P1 y consumido por P0/P2/P3. */
    int pacman_x;
    int pacman_y;
    int pacman_score;
    int pacman_lives;

    int ghost_start_x[NUM_GHOSTS];
    int ghost_start_y[NUM_GHOSTS];

    /*
        BONUS P3 (renderer_process).
        Estado visible de los enemigos: posicion ACTUAL de cada fantasma.
        P2 las publica en cada turno bajo mutex_shared; P3 las lee para dibujar.
    */
    int ghost_x[NUM_GHOSTS];
    int ghost_y[NUM_GHOSTS];

    /* Evento de colisión: P2 lo publica y P0 lo consume/limpia bajo mutex. */
    int collision_detected;
    int collision_tick;
    int collision_ghost_id;

    /* Prioridades efectivas: únicamente P0 debe modificarlas. */
    int prioridad_pacman;
    int prioridad_enemy;

    /* Buzón P1 -> P0: valor y flag se leen/escriben como una sola operación. */
    int pending_priority_pacman;
    int priority_request_active;

    /* Buzón P2 -> P0 equivalente, protegido por el mismo mutex. */
    int pending_priority_enemy;
    int enemy_priority_request_active;

    /*
        Error al abrir o interpretar un archivo de movimientos.
        input_error_process usa 1 para P1 y 2 para P2.
    */
    int input_error;
    int input_error_process;

    /*
        Punto 10: banderas de finalizacion por agotamiento de entradas.

        pacman_moves_finished:
            P1 lo activa cuando ya consumio todas sus instrucciones.

        ghost_moves_finished[i]:
            P2 activa la posicion i cuando ese fantasma agoto su archivo.

        P0 termina la simulacion cuando P1 y los cuatro fantasmas
        agotaron sus entradas.
    */
    int pacman_moves_finished;
    int ghost_moves_finished[NUM_GHOSTS];

    /*
     * Mutex interproceso configurado con PTHREAD_PROCESS_SHARED.
     * Protege todos los campos mutables anteriores. Código que necesita una
     * instantánea de varios campos debe copiarlos manteniendo un único lock.
     */
    pthread_mutex_t mutex_shared;

    /*
        Semáforos del Checkpoint 7.

        sem_pacman_turn:
            P0 libera este semáforo cuando quiere que juegue P1.

        sem_enemy_turn:
            P0 libera este semáforo cuando quiere que juegue P2.

        sem_turn_done:
            P1 o P2 liberan este semáforo cuando terminaron su turno.
    */
    sem_t sem_pacman_turn;
    sem_t sem_enemy_turn;
    sem_t sem_turn_done;

    /*
        BONUS P3 (renderer_process).

        sem_render_turn:
            P0 libera este semaforo cuando quiere que P3 dibuje el estado
            del tick actual (una sola vez por tick).

        sem_render_done:
            P3 libera este semaforo cuando termino de dibujar el cuadro,
            de modo que P0 no adelante el siguiente tick hasta que el
            frame este listo (sincronizacion con global_tick).
    */
    sem_t sem_render_turn;
    sem_t sem_render_done;

} SharedData;

#endif
