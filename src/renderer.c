#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <ncurses.h>

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
    Dibuja un cuadro completo con ncurses a partir de una copia local del
    estado. Se ejecuta FUERA del mutex para no frenar a los demas procesos.

    NO se limpia toda la pantalla en cada cuadro: se reescribe cada celda
    del mapa con mvaddch (posiciones fijas) y se usa clrtoeol solo en las
    lineas de texto de longitud variable. ncurses hace refresh() calculando
    el minimo de cambios, asi que la consola queda dinamica y sin parpadeo.

    Superposicion sobre map_grid ('O' camino, 'X' pared):
    primero Pac-Man ('P') y luego los fantasmas ('A'..'D'), de modo que si
    un fantasma cae sobre Pac-Man se ve la letra del fantasma (colision).
*/
static void dibujar_frame(int filas, int columnas,
                          char grid[MAX_Y][MAX_X],
                          int pacman_y, int pacman_x,
                          int ghost_y[NUM_GHOSTS], int ghost_x[NUM_GHOSTS],
                          int score, int lives, int tick, int game_over) {
    char simbolos[NUM_GHOSTS] = {'A', 'B', 'C', 'D'};

    /* Cabecera: fila 0 y 1; el mapa arranca en la fila 3. */
    const int fila_base = 3;

    mvprintw(0, 0, "========= Pac-Man concurrente | P3 renderer (ncurses) =========");
    mvprintw(1, 0, "Tick: %d", tick);
    clrtoeol();

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

            mvaddch(fila_base + y, x, (chtype)celda);
        }
    }

    mvprintw(fila_base + filas + 1, 0, "Score: %d    Vidas: %d", score, lives);
    clrtoeol();

    if (game_over) {
        mvprintw(fila_base + filas + 2, 0, "===== GAME OVER =====");
        clrtoeol();
    }

    refresh();
}

/*
 * Punto de entrada del proceso P3 ncurses.
 *
 * Espera una autorización sem_render_turn por frame, copia todo el estado bajo
 * mutex_shared y dibuja después de soltarlo. Así obtiene una imagen coherente
 * sin retener el mutex durante E/S lenta. sem_render_done confirma a P0 que el
 * frame solicitado acabó. Si no existe terminal, mantiene igualmente este
 * protocolo para que la simulación headless no se bloquee. No retorna: exit(0).
 */
void renderer_process(SharedData *shared) {
    printf("[P3] renderer_process iniciado\n");
    printf("[P3] PID=%d | PPID=%d\n", getpid(), getppid());

    /*
        Arrancamos ncurses sobre la terminal REAL (/dev/tty) con newterm en
        lugar de initscr(). Ventajas:
          - El usuario puede mandar el debug de P0/P1/P2 (stdout) a un
            archivo con '>' y ncurses sigue dibujando en pantalla.
          - Si no hay terminal (ejecucion headless), newterm devuelve NULL
            y hacemos fallback SIN dibujar, pero manteniendo el handshake
            con P0 para no colgar la simulacion.
        Config pedida: noecho() y curs_set(0) (cursor oculto).
    */
    FILE *tty = fopen("/dev/tty", "r+");
    SCREEN *pantalla = NULL;
    int ui_ok = 0;

    if (tty != NULL) {
        pantalla = newterm(NULL, tty, tty);

        if (pantalla != NULL) {
            set_term(pantalla);
            noecho();
            curs_set(0);
            ui_ok = 1;
        }
    }

    while (1) {
        /*
            Sincronizacion con el tick global: P3 espera a que P0 le de
            permiso en lugar de girar en un loop libre.
        */
        sem_wait(&shared->sem_render_turn);

        /* SECCIÓN CRÍTICA: crear una instantánea; nunca dibujar con el lock. */
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

        if (ui_ok) {
            dibujar_frame(filas, columnas, grid,
                          pacman_y, pacman_x, gy, gx,
                          score, lives, tick, game_over);
        }

        printf("[P3] Frame confirmado para tick %d\n", tick);
        fflush(stdout);

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

    /* Restaurar la terminal a su estado normal. */
    if (ui_ok) {
        endwin();
        delscreen(pantalla);
    }

    if (tty != NULL) {
        fclose(tty);
    }

    printf("[P3] renderer_process finalizado\n");
    exit(0);
}
