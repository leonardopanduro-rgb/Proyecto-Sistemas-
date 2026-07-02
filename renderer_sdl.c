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

static void usar_color(SDL_Renderer *ren, Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
}

/*
    Atiende la cola de eventos SDL para que la ventana no se congele.
    Si el usuario cierra la ventana (SDL_QUIT), pedimos fin ordenado a todo
    el sistema marcando game_over en memoria compartida.
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

/*
    Espera 'ms' milisegundos sin congelar la ventana (sigue atendiendo
    eventos). No es busy loop: cede CPU con SDL_Delay.
*/
static void esperar_atendiendo_eventos(SharedData *shared, Uint32 ms) {
    Uint32 hasta = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < hasta) {
        procesar_eventos(shared);
        SDL_Delay(8);
    }
}

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
    Color ghost_col[NUM_GHOSTS] = {
        {230, 30, 30},    /* A rojo    */
        {255, 120, 190},  /* B rosa    */
        {0, 220, 220},    /* C cyan    */
        {255, 150, 20}    /* D naranja */
    };

    usar_color(ren, negro);
    SDL_RenderClear(ren);

    /* Barra superior + vidas como cuadraditos amarillos. */
    usar_color(ren, gris);
    SDL_Rect barra = {0, 0, columnas * CELL, BAR_H};
    SDL_RenderFillRect(ren, &barra);

    usar_color(ren, amarillo);
    for (int i = 0; i < lives; i++) {
        SDL_Rect vida = {6 + i * 26, 6, 20, 20};
        SDL_RenderFillRect(ren, &vida);
    }

    /* Mapa + entidades. */
    for (int y = 0; y < filas; y++) {
        for (int x = 0; x < columnas; x++) {
            SDL_Rect celda = {x * CELL, BAR_H + y * CELL, CELL, CELL};

            usar_color(ren, (grid[y][x] == 'X') ? azul : negro);
            SDL_RenderFillRect(ren, &celda);

            int hay_entidad = 0;
            Color ec = amarillo;

            if (y == pacman_y && x == pacman_x) {
                hay_entidad = 1;
                ec = amarillo;
            }
            for (int i = 0; i < NUM_GHOSTS; i++) {
                if (y == gy[i] && x == gx[i]) {
                    hay_entidad = 1;
                    ec = ghost_col[i];
                }
            }

            if (hay_entidad) {
                SDL_Rect ent = {x * CELL + 4, BAR_H + y * CELL + 4,
                                CELL - 8, CELL - 8};
                usar_color(ren, ec);
                SDL_RenderFillRect(ren, &ent);
            }
        }
    }

    SDL_RenderPresent(ren);
}

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

        /* 3) Copia corta del estado bajo el MISMO mutex_shared. */
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
