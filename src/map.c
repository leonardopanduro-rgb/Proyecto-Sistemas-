#include <stdio.h>
#include <string.h>

#include "map.h"
#include "shared.h"

/* Retorna 1 para X/O/P/A/B/C/D y 0 para cualquier símbolo inválido. */
static int es_simbolo_mapa_valido(char celda) {
    return celda == 'X' ||
           celda == 'O' ||
           celda == 'P' ||
           celda == 'A' ||
           celda == 'B' ||
           celda == 'C' ||
           celda == 'D';
}

/* Convierte A..D a índices 0..3; retorna -1 si no es un fantasma. */
static int obtener_indice_fantasma(char celda) {
    if (celda >= 'A' && celda <= 'D') {
        return celda - 'A';
    }

    return -1;
}

/*
 * Lee y valida completamente map.txt antes de publicar estado.
 *
 * ruta_mapa es la ruta de entrada y shared es el mmap ya inicializado. Valida
 * tamaño, ancho uniforme, símbolos y exactamente un P/A/B/C/D. Construye todo
 * en variables temporales: si aparece un error, otros procesos nunca reciben
 * un mapa parcial. P0 llama esta función antes de fork(), por lo que todavía
 * no hacen falta locks. Retorna 0 al cargar o 1 ante cualquier error.
 */
int cargar_mapa(const char *ruta_mapa, SharedData *shared) {
    FILE *archivo = fopen(ruta_mapa, "r");

    if (archivo == NULL) {
        perror("Error al abrir map.txt");
        return 1;
    }

    /*
        El espacio adicional permite detectar una fila que supera MAX_X,
        incluso si el archivo usa saltos de linea de Windows.
    */
    char linea[MAX_X + 3];
    char mapa_temporal[MAX_Y][MAX_X];

    int fila = 0;
    int columnas_detectadas = -1;

    int pacman_x = -1;
    int pacman_y = -1;
    int cantidad_pacman = 0;

    int ghost_x[NUM_GHOSTS] = {-1, -1, -1, -1};
    int ghost_y[NUM_GHOSTS] = {-1, -1, -1, -1};
    int cantidad_fantasmas[NUM_GHOSTS] = {0, 0, 0, 0};

    while (fgets(linea, sizeof(linea), archivo) != NULL) {
        int largo = strlen(linea);

        /*
            Quitamos primero '\n' y luego '\r' para aceptar archivos
            creados tanto en Linux como en Windows.
        */
        if (largo > 0 && linea[largo - 1] == '\n') {
            linea[largo - 1] = '\0';
            largo--;
        }

        if (largo > 0 && linea[largo - 1] == '\r') {
            linea[largo - 1] = '\0';
            largo--;
        }

        if (largo == 0) {
            printf("[ERROR] map.txt contiene una fila vacia\n");
            fclose(archivo);
            return 1;
        }

        if (fila >= MAX_Y) {
            printf("[ERROR] map.txt supera el maximo de %d filas\n", MAX_Y);
            fclose(archivo);
            return 1;
        }

        if (largo > MAX_X) {
            printf("[ERROR] La fila %d supera el maximo de %d columnas\n",
                   fila + 1,
                   MAX_X);
            fclose(archivo);
            return 1;
        }

        /*
            La primera fila define el ancho. Todas las demas deben coincidir.
        */
        if (fila == 0) {
            columnas_detectadas = largo;
        } else if (largo != columnas_detectadas) {
            printf("[ERROR] La fila %d tiene %d columnas; se esperaban %d\n",
                   fila + 1,
                   largo,
                   columnas_detectadas);
            fclose(archivo);
            return 1;
        }

        for (int columna = 0; columna < largo; columna++) {
            char celda = linea[columna];

            if (!es_simbolo_mapa_valido(celda)) {
                printf("[ERROR] Simbolo '%c' invalido en fila %d, columna %d\n",
                       celda,
                       fila + 1,
                       columna + 1);
                fclose(archivo);
                return 1;
            }

            if (celda == 'P') {
                cantidad_pacman++;
                pacman_y = fila;
                pacman_x = columna;
                mapa_temporal[fila][columna] = 'O';
            } else {
                int ghost_id = obtener_indice_fantasma(celda);

                if (ghost_id >= 0) {
                    cantidad_fantasmas[ghost_id]++;
                    ghost_y[ghost_id] = fila;
                    ghost_x[ghost_id] = columna;
                    mapa_temporal[fila][columna] = 'O';
                } else {
                    mapa_temporal[fila][columna] = celda;
                }
            }
        }

        fila++;
    }

    if (ferror(archivo)) {
        perror("Error al leer map.txt");
        fclose(archivo);
        return 1;
    }

    fclose(archivo);

    if (fila == 0 || columnas_detectadas <= 0) {
        printf("[ERROR] map.txt esta vacio\n");
        return 1;
    }

    if (cantidad_pacman != 1) {
        printf("[ERROR] Se esperaba exactamente un Pac-Man; se encontraron %d\n",
               cantidad_pacman);
        return 1;
    }

    for (int i = 0; i < NUM_GHOSTS; i++) {
        if (cantidad_fantasmas[i] != 1) {
            printf("[ERROR] Se esperaba exactamente un fantasma %c; se encontraron %d\n",
                   'A' + i,
                   cantidad_fantasmas[i]);
            return 1;
        }
    }

    /*
        Solo despues de validar todo el archivo publicamos el mapa y las
        posiciones en memoria compartida.
    */
    shared->filas = fila;
    shared->columnas = columnas_detectadas;
    shared->pacman_y = pacman_y;
    shared->pacman_x = pacman_x;

    for (int i = 0; i < NUM_GHOSTS; i++) {
        shared->ghost_start_y[i] = ghost_y[i];
        shared->ghost_start_x[i] = ghost_x[i];
    }

    for (int y = 0; y < fila; y++) {
        for (int x = 0; x < columnas_detectadas; x++) {
            shared->map_grid[y][x] = mapa_temporal[y][x];
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

/*
 * Retorna 1 si (y,x) está dentro del rectángulo y no contiene pared X.
 * Comprueba límites antes de indexar map_grid, evitando accesos out-of-bounds.
 * El mapa es inmutable tras cargarlo, por eso leerlo no requiere mutex.
 */
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

/* Imprime el mapa base inmutable; P0 la usa antes de crear procesos hijos. */
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
