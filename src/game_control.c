#include <stdio.h>
#include <pthread.h>

#include "game.h"

/*
    Publica el primer error de entrada observado por P1 o P2.
    mutex_shared evita que dos lectores concurrentes sobrescriban el origen y
    que P0 lea input_error_process a mitad de la actualizacion.
*/
void publicar_error_entrada(SharedData *shared, int process_id) {
    pthread_mutex_lock(&shared->mutex_shared);

    /*
        Conservamos el primer error para saber que proceso lo origino.
    */
    if (shared->input_error == 0) {
        shared->input_error = 1;
        shared->input_error_process = process_id;
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Solo P0 convierte el error publicado en una condicion de finalizacion.
    Retorna 1 si activo game_over. La lectura y la escritura forman una unica
    seccion critica para no perder el evento.
*/
int procesar_error_entrada(SharedData *shared) {
    int process_id = 0;

    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->input_error == 1) {
        process_id = shared->input_error_process;
        shared->game_over = 1;
    }

    pthread_mutex_unlock(&shared->mutex_shared);

    if (process_id != 0) {
        printf("[P0] Error de entrada reportado por P%d\n", process_id);
        printf("[P0] game_over = 1 por archivo o instruccion invalida\n");
        return 1;
    }

    return 0;
}

/*
    P1 publica EOF una sola vez. El mutex evita una carrera entre el executor,
    P0 y cualquier lector del estado de finalizacion.
*/
void publicar_pacman_agotado(SharedData *shared) {
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->pacman_moves_finished == 0) {
        shared->pacman_moves_finished = 1;
        printf("[P1] Sin mas instrucciones: entradas de Pac-Man agotadas\n");
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    P2 publica EOF para un ghost concreto. ghost_id debe estar entre 0 y 3.
    mutex_shared protege el arreglo compartido frente a los cuatro ghost threads.
*/
void publicar_fantasma_agotado(SharedData *shared, int ghost_id) {
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->ghost_moves_finished[ghost_id] == 0) {
        shared->ghost_moves_finished[ghost_id] = 1;
        printf("[P2] Sin mas instrucciones: fantasma %c agotado\n",
               'A' + ghost_id);
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Retorna 1 solo si P1 y los cuatro fantasmas agotaron sus entradas.
    Se toma un snapshot bajo mutex para que P0 no decida con flags mezclados.
*/
int entradas_agotadas(SharedData *shared) {
    int agotadas = 1;

    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->pacman_moves_finished == 0) {
        agotadas = 0;
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (shared->ghost_moves_finished[i] == 0) {
            agotadas = 0;
        }
    }

    pthread_mutex_unlock(&shared->mutex_shared);

    return agotadas;
}

/*
    P1 escribe una solicitud en su buzon, no la prioridad efectiva.
    El mutex vuelve atomica la pareja pending_priority + request_active.
*/
void solicitar_prioridad_pacman(SharedData *shared, int nueva_prioridad) {
    pthread_mutex_lock(&shared->mutex_shared);

    shared->pending_priority_pacman = nueva_prioridad;
    shared->priority_request_active = 1;

    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P1] Solicitud enviada a P0: cambiar prioridad Pac-Man a %d\n",
           nueva_prioridad);
}

/* Equivalente para P2; varios ghosts comparten este unico buzon protegido. */
void solicitar_prioridad_enemy(SharedData *shared, int nueva_prioridad) {
    pthread_mutex_lock(&shared->mutex_shared);

    shared->pending_priority_enemy = nueva_prioridad;
    shared->enemy_priority_request_active = 1;

    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P2] Solicitud enviada a P0: cambiar prioridad Enemigos a %d\n",
           nueva_prioridad);
}

/*
    P0 consume ambos buzones al inicio del siguiente tick.
    Mantiene el mutex durante validar, aplicar y limpiar cada solicitud para que
    ningun productor publique una nueva solicitud en medio del consumo.
*/
void procesar_solicitudes_prioridad(SharedData *shared) {
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->priority_request_active == 1) {
        int nueva = shared->pending_priority_pacman;

        printf("[P0] Solicitud pendiente de P1: prioridad Pac-Man = %d\n",
               nueva);

        if (prioridad_valida(nueva)) {
            shared->prioridad_pacman = nueva;
            printf("[P0] Prioridad de Pac-Man actualizada oficialmente a %d\n",
                   shared->prioridad_pacman);
        } else {
            printf("[P0] Solicitud rechazada: prioridad Pac-Man fuera de rango\n");
        }

        shared->pending_priority_pacman = 0;
        shared->priority_request_active = 0;
    }

    if (shared->enemy_priority_request_active == 1) {
        int nueva = shared->pending_priority_enemy;

        printf("[P0] Solicitud pendiente de P2: prioridad Enemigos = %d\n",
               nueva);

        if (prioridad_valida(nueva)) {
            shared->prioridad_enemy = nueva;
            printf("[P0] Prioridad de Enemigos actualizada oficialmente a %d\n",
                   shared->prioridad_enemy);
        } else {
            printf("[P0] Solicitud rechazada: prioridad Enemigos fuera de rango\n");
        }

        shared->pending_priority_enemy = 0;
        shared->enemy_priority_request_active = 0;
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}

/*
    Copia prioridades bajo mutex y elige 1=P1 o 2=P2.
    Si empatan, ultimo_turno implementa Round Robin alternando el ganador.
    Leer ambas prioridades bajo el mismo lock evita decidir con valores de
    ticks distintos.
*/
int elegir_turno_por_prioridad(SharedData *shared, int *ultimo_turno) {
    int prioridad_pacman;
    int prioridad_enemy;

    pthread_mutex_lock(&shared->mutex_shared);
    prioridad_pacman = shared->prioridad_pacman;
    prioridad_enemy = shared->prioridad_enemy;
    pthread_mutex_unlock(&shared->mutex_shared);

    printf("[P0] Prioridad Pac-Man=%d | Prioridad Enemigos=%d\n",
           prioridad_pacman,
           prioridad_enemy);

    if (prioridad_pacman > prioridad_enemy) {
        printf("[P0] Pac-Man tiene mayor prioridad\n");
        *ultimo_turno = 1;
        return 1;
    }

    if (prioridad_enemy > prioridad_pacman) {
        printf("[P0] Enemigos tienen mayor prioridad\n");
        *ultimo_turno = 2;
        return 2;
    }

    printf("[P0] Empate de prioridades: aplicando Round Robin\n");

    if (*ultimo_turno == 1) {
        *ultimo_turno = 2;
        return 2;
    }

    *ultimo_turno = 1;
    return 1;
}

/*
    Consume en P0 el evento que P2 publico.
    El mismo mutex protege evento, vidas y game_over; asi una colision se
    descuenta exactamente una vez y ningun renderer observa un estado parcial.
*/
void procesar_colision_si_existe(SharedData *shared) {
    /*
        CHECKPOINT 13:
        P0 protege la lectura/escritura del evento de colisión.

        P2 escribe:
        - collision_detected
        - collision_tick
        - collision_ghost_id

        P0 lee esas variables y luego actualiza:
        - pacman_lives
        - game_over
    */
    pthread_mutex_lock(&shared->mutex_shared);

    if (shared->collision_detected == 1) {
        printf("[P0] Evento de colisión recibido\n");
        printf("[P0] collision_tick=%d\n", shared->collision_tick);
        printf("[P0] collision_ghost_id=%d\n", shared->collision_ghost_id);

        shared->pacman_lives--;

        printf("[P0] Vidas restantes de Pac-Man: %d\n",
               shared->pacman_lives);

        if (shared->pacman_lives <= 0) {
            shared->game_over = 1;
            printf("[P0] Pac-Man perdió todas sus vidas\n");
            printf("[P0] game_over = 1\n");
        }

        shared->collision_detected = 0;
        shared->collision_tick = -1;
        shared->collision_ghost_id = -1;
    }

    pthread_mutex_unlock(&shared->mutex_shared);
}



/* Imprime un snapshot consistente manteniendo mutex_shared durante la lectura. */
void imprimir_estado_tick(SharedData *shared) {
    /*
        CHECKPOINT 13:
        Protegemos la lectura del estado compartido.
    */
    pthread_mutex_lock(&shared->mutex_shared);

    printf("[P0] Estado después del tick %d:\n", shared->global_tick);
    printf("     Pac-Man=(%d,%d) | vidas=%d | score=%d | game_over=%d\n",
           shared->pacman_y,
           shared->pacman_x,
           shared->pacman_lives,
           shared->pacman_score,
           shared->game_over);

    pthread_mutex_unlock(&shared->mutex_shared);
}
