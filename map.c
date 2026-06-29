#include <stdio.h>
#include <string.h>

#include "map.h"
#include "shared.h"

int cargar_mapa(const char *ruta_mapa, SharedData *shared) {
    FILE *archivo = fopen(ruta_mapa, "r");

    if (archivo == NULL) {
        perror("Error al abrir map.txt");
        return 1;
    }

    char linea[MAX_X + 10];
    int fila = 0;
    int columnas_detectadas = 0;

    int encontro_pacman = 0;
    int encontro_fantasmas[NUM_GHOSTS] = {0, 0, 0, 0};

    while (fgets(linea, sizeof(linea), archivo) != NULL && fila < MAX_Y) {
        int largo = strlen(linea);

        /*
            Si el archivo viene con salto de línea Linux:
            "XXXXXXXXXX\n"
            quitamos el '\n'.
        */
        if (largo > 0 && linea[largo - 1] == '\n') {
            linea[largo - 1] = '\0';
            largo--;
        }

        /*
            Si el archivo viene con salto de línea Windows:
            "XXXXXXXXXX\r\n"
            después de quitar '\n' todavía queda '\r'.
            Por eso también lo quitamos.
        */
        if (largo > 0 && linea[largo - 1] == '\r') {
            linea[largo - 1] = '\0';
            largo--;
        }

        /*
            Si la línea quedó vacía, la ignoramos.
        */
        if (largo == 0) {
            continue;
        }

        /*
            La primera fila válida define la cantidad de columnas.
        */
        if (fila == 0) {
            columnas_detectadas = largo;
        }

        for (int columna = 0; columna < largo && columna < MAX_X; columna++) {
            char celda = linea[columna];

            /*
                Guardamos el mapa base como transitable si encontramos
                P, A, B, C o D, porque sus posiciones ya se guardan aparte.

                Es decir:
                - P se guarda en shared->pacman_y / shared->pacman_x.
                - A, B, C, D se guardan en ghost_start_y / ghost_start_x.
                - En el mapa base se pone 'O' para que esa celda sea caminable.
            */
            if (celda == 'P') {
                shared->pacman_y = fila;
                shared->pacman_x = columna;
                shared->map_grid[fila][columna] = 'O';
                encontro_pacman = 1;
            } else if (celda == 'A') {
                shared->ghost_start_y[0] = fila;
                shared->ghost_start_x[0] = columna;
                shared->map_grid[fila][columna] = 'O';
                encontro_fantasmas[0] = 1;
            } else if (celda == 'B') {
                shared->ghost_start_y[1] = fila;
                shared->ghost_start_x[1] = columna;
                shared->map_grid[fila][columna] = 'O';
                encontro_fantasmas[1] = 1;
            } else if (celda == 'C') {
                shared->ghost_start_y[2] = fila;
                shared->ghost_start_x[2] = columna;
                shared->map_grid[fila][columna] = 'O';
                encontro_fantasmas[2] = 1;
            } else if (celda == 'D') {
                shared->ghost_start_y[3] = fila;
                shared->ghost_start_x[3] = columna;
                shared->map_grid[fila][columna] = 'O';
                encontro_fantasmas[3] = 1;
            } else {
                shared->map_grid[fila][columna] = celda;
            }
        }

        fila++;
    }

    fclose(archivo);

    shared->filas = fila;
    shared->columnas = columnas_detectadas;

    if (!encontro_pacman) {
        printf("[ERROR] El mapa no tiene Pac-Man marcado con P\n");
        return 1;
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (!encontro_fantasmas[i]) {
            printf("[ERROR] El mapa no tiene el fantasma %d\n", i + 1);
            return 1;
        }
    }

    printf("Mapa cargado: %d filas x %d columnas\n",
           shared->filas,
           shared->columnas);

    printf("Pac-Man inicia en (%d,%d)\n",
           shared->pacman_y,
           shared->pacman_x);

    printf("Fantasma A inicia en (%d,%d)\n",
           shared->ghost_start_y[0],
           shared->ghost_start_x[0]);

    printf("Fantasma B inicia en (%d,%d)\n",
           shared->ghost_start_y[1],
           shared->ghost_start_x[1]);

    printf("Fantasma C inicia en (%d,%d)\n",
           shared->ghost_start_y[2],
           shared->ghost_start_x[2]);

    printf("Fantasma D inicia en (%d,%d)\n",
           shared->ghost_start_y[3],
           shared->ghost_start_x[3]);

    return 0;
}

int es_celda_valida(SharedData *shared, int y, int x) {
    if (y < 0 || y >= shared->filas) {
        return 0;
    }

    if (x < 0 || x >= shared->columnas) {
        return 0;
    }

    if (shared->map_grid[y][x] == 'X') {
        return 0;
    }

    return 1;
}

void imprimir_mapa(SharedData *shared) {
    printf("\nMapa base cargado en shared->map_grid:\n");

    for (int y = 0; y < shared->filas; y++) {
        for (int x = 0; x < shared->columnas; x++) {
            printf("%c", shared->map_grid[y][x]);
        }
        printf("\n");
    }

    printf("\n");
}

