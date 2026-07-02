#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <SDL2/SDL.h>

#include "renderer.h"
#include "shared.h"

/*
    Version SDL2 de P3 (renderer_process).

    Misma sincronizacion que la version de consola: P3 redibuja SOLO cuando
    P0 avanza el tick (sem_render_turn / sem_render_done) y copia el estado
    bajo mutex_shared con locks cortos, dibujando fuera del lock.

    Solo rectangulos de colores (sin SDL_image ni SDL_ttf):
      - pared 'X' azul, camino 'O' negro
      - Pac-Man amarillo
      - fantasmas A rojo, B rosa, C cyan, D naranja
      - vidas: cuadraditos amarillos en la barra superior
      - score: se imprime en la terminal (sin fuentes TTF)
*/

#define CELL 32          /* pixeles por celda del mapa */
#define BAR_H 32         /* alto de la barra superior (vidas) */
#define FRAME_DELAY_MS 300   /* ritmo de animacion (ms por tick); 0 = sin pausa */

typedef struct { Uint8 r, g, b; } Color;

/* Traduce Color al estado de dibujo del renderer; no accede a SharedData. */
static void usar_color(SDL_Renderer *ren, Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
}

/*
 * Atiende eventos SDL. SDL_QUIT publica game_over bajo mutex_shared: usar el
 * mismo mutex que P0 y los demás lectores evita una carrera interproceso.
 */
static void procesar_eventos(SharedData *shared) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            pthread_mutex_lock(&shared->mutex_shared);
            shared->game_over = 1;
            pthread_mutex_unlock(&shared->mutex_shared);
        }
    }
}

/* Espera ms atendiendo eventos y cediendo CPU con SDL_Delay, sin busy wait. */
static void esperar_atendiendo_eventos(SharedData *shared, Uint32 ms) {
    Uint32 hasta = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < hasta) {
        procesar_eventos(shared);
        SDL_Delay(8);
    }
}

/*
 * Dibuja mapa, vidas y entidades desde una instantánea local. No toma mutex ni
 * modifica SharedData, de modo que una operación gráfica lenta no frena P1/P2.
 */
static void dibujar_frame(SDL_Renderer *ren,
                          int filas, int columnas,
                          char grid[MAX_Y][MAX_X],
                          int pacman_y, int pacman_x,
                          int gy[NUM_GHOSTS], int gx[NUM_GHOSTS],
                          int lives) {
    Color negro    = {0, 0, 0};
    Color azul     = {40, 60, 220};
    Color amarillo = {255, 220, 0};
    Color gris     = {30, 30, 30};
    Color blanco   = {255, 255, 255};
    Color azul_ojo = {0, 0, 255};
    
    Color ghost_col[NUM_GHOSTS] = {
        {230, 30, 30},    /* A rojo    */
        {255, 120, 190},  /* B rosa    */
        {0, 220, 220},    /* C cyan    */
        {255, 150, 20}    /* D naranja */
    };

    // Variables estáticas para recordar la posición del tick anterior y la dirección de la boca
    static int prev_px = -1;
    static int prev_py = -1;
    static enum { DIR_RIGHT, DIR_LEFT, DIR_UP, DIR_DOWN } ultima_dir = DIR_RIGHT;

    // Calcular la dirección real basándonos en el desplazamiento desde el último tick
    if (prev_px != -1 && prev_py != -1) {
        if (pacman_x > prev_px) ultima_dir = DIR_RIGHT;
        else if (pacman_x < prev_px) ultima_dir = DIR_LEFT;
        else if (pacman_y > prev_py) ultima_dir = DIR_DOWN;
        else if (pacman_y < prev_py) ultima_dir = DIR_UP;
    }
    
    // Actualizar el historial para el siguiente tick
    prev_px = pacman_x;
    prev_py = pacman_y;

    usar_color(ren, negro);
    SDL_RenderClear(ren);

    /* Barra superior + vidas */
    usar_color(ren, gris);
    SDL_Rect barra = {0, 0, columnas * CELL, BAR_H};
    SDL_RenderFillRect(ren, &barra);

    for (int i = 0; i < lives; i++) {
        int vx = 6 + i * 26;
        int vy = 6;
        usar_color(ren, amarillo);
        SDL_Rect base_vida = {vx, vy, 20, 20};
        SDL_RenderFillRect(ren, &base_vida);
        usar_color(ren, gris);
        SDL_Rect boca_vida = {vx + 10, vy + 5, 10, 10};
        SDL_RenderFillRect(ren, &boca_vida);
    }

    /* Laberinto y Entidades */
    for (int y = 0; y < filas; y++) {
        for (int x = 0; x < columnas; x++) {
            int cx = x * CELL;
            int cy = BAR_H + y * CELL;

            if (grid[y][x] == 'X') {
                usar_color(ren, azul);
                SDL_Rect celda = {cx, cy, CELL, CELL};
                SDL_RenderFillRect(ren, &celda);
                continue;
            }

            // --- RENDER DE PAC-MAN DINÁMICO ---
            if (y == pacman_y && x == pacman_x) {
                usar_color(ren, amarillo);
                
                // Cuerpo Base
                SDL_Rect cuerpo_centro = {cx + 4, cy + 4, CELL - 8, CELL - 8};
                SDL_Rect cuerpo_alto   = {cx + 8, cy + 2, CELL - 16, CELL - 4};
                SDL_Rect cuerpo_ancho  = {cx + 2, cy + 8, CELL - 4, CELL - 16};
                SDL_RenderFillRect(ren, &cuerpo_centro);
                SDL_RenderFillRect(ren, &cuerpo_alto);
                SDL_RenderFillRect(ren, &cuerpo_ancho);

                // Dibujar Ojo y Boca según la dirección de movimiento calculada
                usar_color(ren, negro);
                SDL_Rect ojo;
                SDL_Rect boca;

                switch (ultima_dir) {
                    case DIR_RIGHT:
                        ojo = (SDL_Rect){cx + 12, cy + 6, 4, 4};
                        boca = (SDL_Rect){cx + CELL - 12, cy + 10, 12, 12};
                        break;
                    case DIR_LEFT:
                        ojo = (SDL_Rect){cx + 14, cy + 6, 4, 4};
                        boca = (SDL_Rect){cx, cy + 10, 12, 12};
                        break;
                    case DIR_UP:
                        ojo = (SDL_Rect){cx + 6, cy + 14, 4, 4};
                        boca = (SDL_Rect){cx + 10, cy, 12, 12};
                        break;
                    case DIR_DOWN:
                        ojo = (SDL_Rect){cx + 22, cy + 14, 4, 4};
                        boca = (SDL_Rect){cx + 10, cy + CELL - 12, 12, 12};
                        break;
                }
                
                SDL_RenderFillRect(ren, &ojo);
                SDL_RenderFillRect(ren, &boca);
                continue;
            }

            // --- RENDER DE FANTASMAS ---
            for (int i = 0; i < NUM_GHOSTS; i++) {
                if (y == gy[i] && x == gx[i]) {
                    usar_color(ren, ghost_col[i]);

                    SDL_Rect tronco = {cx + 4, cy + 6, CELL - 8, CELL - 12};
                    SDL_Rect domo   = {cx + 6, cy + 3, CELL - 12, 4};
                    SDL_RenderFillRect(ren, &tronco);
                    SDL_RenderFillRect(ren, &domo);

                    SDL_Rect pie_izq  = {cx + 4,  cy + CELL - 7, 6, 4};
                    SDL_Rect pie_med  = {cx + 13, cy + CELL - 7, 6, 4};
                    SDL_Rect pie_der  = {cx + 22, cy + CELL - 7, 6, 4};
                    SDL_RenderFillRect(ren, &pie_izq);
                    SDL_RenderFillRect(ren, &pie_med);
                    SDL_RenderFillRect(ren, &pie_der);

                    usar_color(ren, blanco);
                    SDL_Rect ojo_izq_blanco = {cx + 8,  cy + 8, 6, 8};
                    SDL_Rect ojo_der_blanco = {cx + 18, cy + 8, 6, 8};
                    SDL_RenderFillRect(ren, &ojo_izq_blanco);
                    SDL_RenderFillRect(ren, &ojo_der_blanco);

                    usar_color(ren, azul_ojo);
                    int offset_pupila = (i % 2 == 0) ? 0 : 2;
                    SDL_Rect pupila_izq = {cx + 8 + offset_pupila,  cy + 10, 4, 4};
                    SDL_Rect pupila_der = {cx + 18 + offset_pupila, cy + 10, 4, 4};
                    SDL_RenderFillRect(ren, &pupila_izq);
                    SDL_RenderFillRect(ren, &pupila_der);
                }
            }
        }
    }

    SDL_RenderPresent(ren);
}
/*
 * Punto de entrada de la variante SDL de P3.
 *
 * Conserva el protocolo exacto sem_render_turn -> snapshot con mutex_shared ->
 * dibujo sin lock -> sem_render_done. En modo headless confirma cada barrera
 * aunque no cree ventana, evitando congelar P0. Destruye objetos SDL y termina
 * el hijo con exit(0).
 */
void renderer_process(SharedData *shared) {
    printf("[P3-SDL] renderer_process iniciado\n");
    printf("[P3-SDL] PID=%d | PPID=%d\n", getpid(), getppid());

    /* Dimensiones del mapa: P0 ya lo cargo antes del fork. */
    pthread_mutex_lock(&shared->mutex_shared);
    int filas0 = shared->filas;
    int columnas0 = shared->columnas;
    pthread_mutex_unlock(&shared->mutex_shared);

    SDL_Window *window = NULL;
    SDL_Renderer *ren = NULL;
    int ui_ok = 0;

    if (SDL_Init(SDL_INIT_VIDEO) == 0) {
        window = SDL_CreateWindow(
            "Pac-Man concurrente (P3 SDL2)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            columnas0 * CELL, BAR_H + filas0 * CELL,
            SDL_WINDOW_SHOWN);

        if (window != NULL) {
            ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
            if (ren == NULL) {
                ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            }
            if (ren != NULL) {
                ui_ok = 1;
            }
        }
    }

    if (!ui_ok) {
        printf("[P3-SDL] Sin ventana (headless o error SDL: %s).\n",
               SDL_GetError());
        printf("[P3-SDL] Sigo cerrando la barrera del tick sin dibujar.\n");
    }

    while (1) {
        /*
            Mismo protocolo bloqueante que el renderer ncurses de referencia:
            un sem_wait consumido equivale exactamente a un frame solicitado.
        */
        sem_wait(&shared->sem_render_turn);

        if (ui_ok) {
            procesar_eventos(shared);
        }

        /* SECCIÓN CRÍTICA corta: copiar un frame consistente bajo el mutex. */
        char grid[MAX_Y][MAX_X];
        int filas, columnas, pacman_y, pacman_x;
        int score, lives, tick, game_over;
        int gy[NUM_GHOSTS], gx[NUM_GHOSTS];

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

        /* 4) Dibujar fuera del lock; score a la terminal (sin TTF). */
        if (ui_ok) {
            dibujar_frame(ren, filas, columnas, grid,
                          pacman_y, pacman_x, gy, gx, lives);
        }
        printf("[P3-SDL] Tick %d | Score: %d | Vidas: %d\n",
               tick, score, lives);
        fflush(stdout);

        /* 5) Cerrar la barrera del tick con P0. */
        sem_post(&shared->sem_render_done);

        /* 6) Fin del juego: ultimo frame 2 s y salir; si no, ritmo normal. */
        if (game_over) {
            if (ui_ok) {
                esperar_atendiendo_eventos(shared, 2000);
            }
            break;
        } else if (ui_ok && FRAME_DELAY_MS > 0) {
            esperar_atendiendo_eventos(shared, FRAME_DELAY_MS);
        }
    }

    /* 7) Cierre ordenado de SDL. */
    if (ui_ok) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(window);
    }
    SDL_Quit();

    printf("[P3-SDL] renderer_process finalizado\n");
    exit(0);
}
