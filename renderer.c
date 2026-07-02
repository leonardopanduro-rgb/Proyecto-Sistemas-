#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "renderer.h"
#include "shared.h"

/*
    Pausa entre cuadros (microsegundos) para poder observar la animacion.
    Va FUERA de cualquier lock y DESPUES de avisar a P0, asi que no retiene
    la memoria compartida. Baja a 0 si no quieres pausa alguna, o sube el
    valor (p. ej. 400000 = 0.4 s) para ver el tablero mas lento.
*/
#define RENDER_DELAY_US 200000

/*
    Dibuja un cuadro completo a partir de una copia local del estado.
    Se ejecuta FUERA del mutex para no frenar a los demas procesos.

    'out' es normalmente la terminal real (/dev/tty), de modo que el ruido
    de depuracion de P0/P1/P2 (que va por stdout) se pueda redirigir a un
    archivo y en pantalla quede solo la animacion.

    Superposicion sobre map_grid ('O' camino, 'X' pared):
    primero Pac-Man ('P') y luego los fantasmas ('A'..'D'), de modo que si
    un fantasma cae sobre Pac-Man se ve la letra del fantasma (colision).
*/
static void dibujar_frame(FILE *out,
                          int filas, int columnas,
                          char grid[MAX_Y][MAX_X],
                          int pacman_y, int pacman_x,
                          int ghost_y[NUM_GHOSTS], int ghost_x[NUM_GHOSTS],
                          int score, int lives, int tick, int game_over) {
    char simbolos[NUM_GHOSTS] = {'A', 'B', 'C', 'D'};

    /*
        Anti-parpadeo: limpiar pantalla y llevar el cursor al inicio con
        codigos ANSI antes de cada redibujado (una sola vez por tick).
    */
    fprintf(out, "\033[2J\033[H");

    fprintf(out, "========= Pac-Man concurrente | P3 renderer =========\n");
    fprintf(out, "Tick: %d\n\n", tick);

    for (int y = 0; y < filas; y++) {
        for (int x = 0; x < columnas; x++) {
            char celda = grid[y][x];

            if (y == pacman_y && x == pacman_x) {
                celda = 'P';
            }

            for (int i = 0; i < NUM_GHOSTS; i++) {
                if (y == ghost_y[i] && x == ghost_x[i]) {
                    celda = simbolos[i];
                }
            }

            fputc(celda, out);
        }
        fputc('\n', out);
    }

    fprintf(out, "\nScore: %d    Vidas: %d\n", score, lives);

    if (game_over) {
        fprintf(out, "\n===== GAME OVER =====\n");
    }

    fflush(out);
}

void renderer_process(SharedData *shared) {
    printf("[P3] renderer_process iniciado\n");
    printf("[P3] PID=%d | PPID=%d\n", getpid(), getppid());

    /*
        Dibujamos en la terminal real (/dev/tty) en lugar de stdout. Asi el
        usuario puede mandar TODO el debug de P0/P1/P2 a un archivo, p. ej.:

            ./pacman_concurrente Caso1 40 --render > debug.log 2>&1

        y ver SOLO la animacion limpia en pantalla. Si no hay terminal
        (ejecucion sin tty), caemos a stdout sin fallar.
    */
    FILE *out = fopen("/dev/tty", "w");
    if (out == NULL) {
        out = stdout;
    }

    while (1) {
        /*
            Sincronizacion con el tick global: P3 espera a que P0 le de
            permiso en lugar de girar en un loop libre.
        */
        sem_wait(&shared->sem_render_turn);

        /*
            Copia del estado bajo el MISMO mutex que protege el resto de
            la memoria compartida. Solo se copia (rapido); el dibujado va
            despues, fuera del lock, para no bloquear a P1/P2.
        */
        char grid[MAX_Y][MAX_X];
        int filas, columnas;
        int pacman_y, pacman_x;
        int score, lives, tick, game_over;
        int gy[NUM_GHOSTS];
        int gx[NUM_GHOSTS];

        pthread_mutex_lock(&shared->mutex_shared);

        filas = shared->filas;
        columnas = shared->columnas;
        pacman_y = shared->pacman_y;
        pacman_x = shared->pacman_x;
        score = shared->pacman_score;
        lives = shared->pacman_lives;
        tick = shared->global_tick;
        game_over = shared->game_over;

        for (int i = 0; i < NUM_GHOSTS; i++) {
            gy[i] = shared->ghost_y[i];
            gx[i] = shared->ghost_x[i];
        }

        for (int y = 0; y < filas; y++) {
            for (int x = 0; x < columnas; x++) {
                grid[y][x] = shared->map_grid[y][x];
            }
        }

        pthread_mutex_unlock(&shared->mutex_shared);

        dibujar_frame(out, filas, columnas, grid,
                      pacman_y, pacman_x, gy, gx,
                      score, lives, tick, game_over);

        /*
            Avisar a P0 que el cuadro ya se dibujo (cierra la barrera del
            tick: P0 no adelanta el siguiente tick hasta este post).
        */
        sem_post(&shared->sem_render_done);

        /*
            Terminacion ordenada: tras dibujar el cuadro final salimos,
            igual que P1 y P2.
        */
        if (game_over) {
            break;
        }

        if (RENDER_DELAY_US > 0) {
            usleep(RENDER_DELAY_US);
        }
    }

    if (out != stdout) {
        fclose(out);
    }

    printf("[P3] renderer_process finalizado\n");
    exit(0);
}
