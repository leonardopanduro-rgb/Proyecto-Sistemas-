#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>

#include "game.h"
#include "p0_threads.h"
#include "map.h"
#include "renderer.h"

/*
 * P0: propietario del ciclo de vida y del scheduler global.
 *
 * carpeta_caso contiene mapa y movimientos; max_ticks_arg reemplaza el límite
 * por defecto si es positivo; render_enabled decide si se crea P3. Retorna 0
 * si la simulación y todos los hijos terminan correctamente, o 1 ante entrada
 * inválida, fork/wait fallido o hijo con error.
 *
 * Flujo: crea mmap y sincronización -> valida mapa -> fork P1/P2/P3 -> crea
 * tres hilos de P0 -> ejecuta ticks completos -> publica game_over -> despierta
 * y une hilos/hijos -> destruye recursos. P0 es el único que avanza ticks,
 * decide turnos y descuenta vidas, evitando escrituras competidoras.
 */
int scheduler_process(const char *carpeta_caso, int max_ticks_arg, int render_enabled) {
    printf("Pac-Man concurrente POSIX - Checkpoint 13\n");
    printf("[P0] scheduler_process con mitigación de race conditions\n");


    printf("[P0] PID=%d\n", getpid());

    /*
        1. Crear e inicializar memoria compartida.
    */
    SharedData *shared = crear_memoria_compartida();
    inicializar_shared(shared);

    /*
        Punto 10: si se recibio un max_ticks valido, reemplaza al valor
        por defecto fijado en inicializar_shared.
    */
    if (max_ticks_arg > 0) {
        shared->max_ticks = max_ticks_arg;
    }

    /*
        2. Construir ruta del mapa.
    */
    char ruta_mapa[256];
    construir_ruta(ruta_mapa, sizeof(ruta_mapa), carpeta_caso, "map.txt");

    printf("[P0] Leyendo mapa: %s\n", ruta_mapa);

    /*
        3. Cargar mapa en memoria compartida.
    */
    if (cargar_mapa(ruta_mapa, shared) != 0) {
        printf("[ERROR] No se pudo cargar el mapa\n");
        liberar_memoria_compartida(shared);
        return 1;
    }

    imprimir_mapa(shared);

    /*
        BONUS P3: dejar visibles las posiciones iniciales de los fantasmas
        para que P3 pueda dibujarlas desde el primer cuadro, antes incluso
        de que P2 tenga su primer turno.
    */
    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_x[i] = shared->ghost_start_x[i];
        shared->ghost_y[i] = shared->ghost_start_y[i];
    }

    printf("[P0] Estado compartido base inicializado\n");
    printf("[P0] Pac-Man en shared: (%d,%d)\n",
           shared->pacman_y,
           shared->pacman_x);

    printf("[P0] Vidas iniciales: %d\n",
           shared->pacman_lives);

    printf("[P0] max_ticks = %d\n",
           shared->max_ticks);

    printf("[P0] Prioridades iniciales: Pac-Man=%d, Enemigos=%d\n",
           shared->prioridad_pacman,
           shared->prioridad_enemy);

    /*
        4. Crear P1 = pacman_process.
    */
    /* Evita que los hijos hereden y repitan salida que stdout aun no vacio. */
    fflush(NULL);

    pid_t pid_p0 = getpid();
    pid_t pid_pacman = fork();

    if (pid_pacman < 0) {
        perror("[P0] Error al crear P1");
        liberar_memoria_compartida(shared);
        return 1;
    }

    if (pid_pacman == 0) {
        configurar_muerte_con_padre(pid_p0);
        pacman_process(shared, carpeta_caso);
    }

    /*
        5. Crear P2 = enemy_process.
    */
    pid_t pid_enemy = fork();

    if (pid_enemy < 0) {
        perror("[P0] Error al crear P2");
        terminar_hijos_por_error(shared, pid_pacman, -1, -1);
        liberar_memoria_compartida(shared);
        return 1;
    }

    if (pid_enemy == 0) {
        configurar_muerte_con_padre(pid_p0);
        enemy_process(shared, carpeta_caso);
    }

    /*
        BONUS: Crear P3 = renderer_process (solo si se paso --render).
        Este fork lo ejecuta unicamente P0: P1 y P2 llaman a exit() dentro
        de sus funciones, por lo que nunca llegan a esta linea.
    */
    pid_t pid_render = -1;

    if (render_enabled) {
        pid_render = fork();

        if (pid_render < 0) {
            perror("[P0] Error al crear P3");
            terminar_hijos_por_error(shared, pid_pacman, pid_enemy, -1);
            liberar_memoria_compartida(shared);
            return 1;
        }

        if (pid_render == 0) {
            configurar_muerte_con_padre(pid_p0);
            renderer_process(shared);
        }
    }

    printf("[P0] Procesos creados: P1 PID=%d, P2 PID=%d\n",
           pid_pacman,
           pid_enemy);

    if (render_enabled) {
        printf("[P0] Proceso P3 (renderer) PID=%d\n", pid_render);
    }

    /*
        ultimo_turno sirve para Round Robin.
        Lo iniciamos en 2 para que, si hay empate,
        el primer turno sea P1.
    */
    int finalizo_por_error = 0;
    int finalizo_por_agotamiento = 0;

    /*
        Punto 2 (Parte B): P0 crea sus tres hilos internos.
    */
    SchedulerThreadData sched;
    inicializar_scheduler_thread_data(&sched, shared);

    pthread_t hilo_tick;
    pthread_t hilo_scheduler;
    pthread_t hilo_signal;

    pthread_create(&hilo_tick, NULL, tick_thread, &sched);
    pthread_create(&hilo_scheduler, NULL, scheduler_thread, &sched);
    pthread_create(&hilo_signal, NULL, signal_thread, &sched);

    /*
     * Cada vuelta es una transacción de tick:
     * tick_thread -> scheduler_thread -> signal_thread -> P1/P2 -> turn_done.
     * sem_tick_finished solo llega después de esa cadena, por lo que no puede
     * existir más de un proceso ejecutando una acción lógica por tick.
     */
    while (1) {
        int game_over;
        int global_tick;
        int max_ticks;
        int pacman_lives;

        obtener_control_juego(shared,
                              &game_over,
                              &global_tick,
                              &max_ticks,
                              &pacman_lives);

        if (game_over || global_tick >= max_ticks || pacman_lives <= 0) {
            break;
        }

        if (procesar_error_entrada(shared)) {
            finalizo_por_error = 1;
            break;
        }

        /*
            Punto 10: si P1 y los cuatro fantasmas agotaron sus entradas,
            P0 finaliza en vez de seguir repartiendo turnos vacios.
        */
        if (entradas_agotadas(shared)) {
            finalizo_por_agotamiento = 1;
            break;
        }

        /* Barrera: P0 principal no procesa estado hasta finalizar el turno. */
        sem_post(&sched.sem_tick_start);
        sem_wait(&sched.sem_tick_finished);

        if (procesar_error_entrada(shared)) {
            finalizo_por_error = 1;
            break;
        }

        /* P0 consume bajo mutex el evento de P2 y es el único que baja vidas. */
        procesar_colision_si_existe(shared);

        imprimir_estado_tick(shared);

        /*
            BONUS: pedir a P3 que dibuje el estado de este tick y esperar a
            que termine (exactamente un redibujado por tick). P3 solo lee la
            memoria y toma mutex_shared un instante, asi que no frena a P1/P2.
        */
        if (render_enabled) {
            sem_post(&shared->sem_render_turn);

            int estado_renderer = esperar_renderer_done(shared, pid_render);

            if (estado_renderer <= 0) {
                if (estado_renderer == 0) {
                    kill(pid_render, SIGTERM);
                    waitpid(pid_render, NULL, 0);
                }

                pid_render = -1;
                render_enabled = 0;
                printf("[P0] Renderer deshabilitado; la simulación continúa\n");
            }
        }
    }

    /*
        7. Revisar causa de finalización.
    */
    if (finalizo_por_error) {
        printf("\n[P0] Fin por error de entrada\n");
    } else if (finalizo_por_agotamiento) {
        printf("\n[P0] Fin por agotamiento de entradas de P1 y P2\n");
        establecer_game_over(shared);
    } else {
        int game_over;
        int global_tick;
        int max_ticks;
        int pacman_lives;

        obtener_control_juego(shared,
                              &game_over,
                              &global_tick,
                              &max_ticks,
                              &pacman_lives);

        if (pacman_lives <= 0) {
        printf("\n[P0] Fin por vidas agotadas\n");
        } else if (global_tick >= max_ticks) {
            printf("\n[P0] Se alcanzó max_ticks\n");
            establecer_game_over(shared);
        }
    }

    printf("\n[P0] Condición de finalización detectada\n");
    printf("[P0] game_over = %d\n", obtener_game_over(shared));

    /*
     * Cierre de hilos P0: la bandera se escribe bajo mutex_scheduler y cada
     * sem_post libera una fase que pudiera estar dormida. Los joins ocurren
     * antes de destruir sus semáforos y mutex.
     */
    marcar_scheduler_terminar(&sched);
    sem_post(&sched.sem_tick_start);
    sem_post(&sched.sem_tick_ready);
    sem_post(&sched.sem_turn_ready);

    pthread_join(hilo_tick, NULL);
    pthread_join(hilo_scheduler, NULL);
    pthread_join(hilo_signal, NULL);

    destruir_scheduler_thread_data(&sched);

    /* game_over ya está publicado; despertar P1/P2 les permite observarlo. */
    sem_post(&shared->sem_pacman_turn);
    sem_post(&shared->sem_enemy_turn);

    /*
        BONUS: liberar a P3 si quedo esperando turno de dibujo. Aqui
        game_over ya vale 1, asi que P3 dibuja el cuadro final y sale.
    */
    if (render_enabled) {
        sem_post(&shared->sem_render_turn);
    }

    /* waitpid de todos los hijos evita zombies y precede a munmap(). */
    int hijos_ok = 1;

    if (!esperar_hijo_correctamente(pid_pacman, "P1")) {
        hijos_ok = 0;
    }

    if (!esperar_hijo_correctamente(pid_enemy, "P2")) {
        hijos_ok = 0;
    }

    if (render_enabled) {
        if (!esperar_hijo_correctamente(pid_render, "P3")) {
            hijos_ok = 0;
        }
    }

    /* Último paso: nadie conserva ya una referencia válida al mmap. */
    liberar_memoria_compartida(shared);

    printf("[P0] Recursos liberados\n");
    printf("Fin de Checkpoint 13\n");

    return (finalizo_por_error || !hijos_ok) ? 1 : 0;
}
