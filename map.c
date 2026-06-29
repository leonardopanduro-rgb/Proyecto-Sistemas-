#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>

#include "map.h"
#include "utils.h"

char mapa[MAX_FILAS][MAX_COLUMNAS];

int filas = 0;
int columnas = 0;

int pacman_x = -1;
int pacman_y = -1;
int pacman_lives = 3;

int ghost_x[4];
int ghost_y[4];

void inicializar_mapa() {
    int i;
    int j;

    filas = 0;
    columnas = 0;

    pacman_x = -1;
    pacman_y = -1;
    pacman_lives = 3;

    for (i = 0; i < MAX_FILAS; i++) {
        for (j = 0; j < MAX_COLUMNAS; j++) {
            mapa[i][j] = ' ';
        }
    }

    for (i = 0; i < 4; i++) {
        ghost_x[i] = -1;
        ghost_y[i] = -1;
    }
}

void revisar_personaje(char c, int fila, int columna) {
    if (c == 'P') {
        pacman_x = columna;
        pacman_y = fila;
    }

    if (c == 'A') {
        ghost_x[0] = columna;
        ghost_y[0] = fila;
    }

    if (c == 'B') {
        ghost_x[1] = columna;
        ghost_y[1] = fila;
    }

    if (c == 'C') {
        ghost_x[2] = columna;
        ghost_y[2] = fila;
    }

    if (c == 'D') {
        ghost_x[3] = columna;
        ghost_y[3] = fila;
    }
}

int caracter_valido(char c) {
    if (c == 'X') return 1;
    if (c == 'O') return 1;
    if (c == 'P') return 1;
    if (c == 'A') return 1;
    if (c == 'B') return 1;
    if (c == 'C') return 1;
    if (c == 'D') return 1;

    return 0;
}

int cargar_mapa(const char ruta_mapa[]) {
    char buffer[BUFFER_SIZE];

    int fd = syscall(SYS_open, ruta_mapa, O_RDONLY);

    if (fd < 0) {
        escribir_error("Error: no se pudo abrir el archivo del mapa\n");
        escribir_error("Ruta usada: ");
        escribir_error(ruta_mapa);
        escribir_error("\n");
        return 0;
    }

    int bytes_leidos = syscall(SYS_read, fd, buffer, BUFFER_SIZE - 1);

    if (bytes_leidos <= 0) {
        escribir_error("Error: no se pudo leer el mapa\n");
        syscall(SYS_close, fd);
        return 0;
    }

    syscall(SYS_close, fd);

    int fila = 0;
    int columna = 0;
    int i = 0;
    int ancho_detectado = -1;
    int cantidad_pacman = 0;

    while (i < bytes_leidos) {
        char c = buffer[i];

        if (c == '\r') {
            i++;
            continue;
        }

        if (c == '\n') {
            if (columna > 0) {
                if (ancho_detectado == -1) {
                    ancho_detectado = columna;
                } else {
                    if (columna != ancho_detectado) {
                        escribir_error("Error: las filas del mapa no tienen el mismo tamaño\n");
                        return 0;
                    }
                }

                fila++;
                columna = 0;
            }

            i++;
            continue;
        }

        if (fila >= MAX_FILAS || columna >= MAX_COLUMNAS) {
            escribir_error("Error: mapa demasiado grande\n");
            return 0;
        }

        if (!caracter_valido(c)) {
            escribir_error("Error: caracter invalido en el mapa: ");
            syscall(SYS_write, 2, &c, 1);
            escribir_error("\n");
            return 0;
        }

        mapa[fila][columna] = c;

        if (c == 'P') {
            cantidad_pacman++;
        }

        revisar_personaje(c, fila, columna);

        columna++;
        i++;
    }

    if (columna > 0) {
        if (ancho_detectado == -1) {
            ancho_detectado = columna;
        } else {
            if (columna != ancho_detectado) {
                escribir_error("Error: la ultima fila tiene tamaño diferente\n");
                return 0;
            }
        }

        fila++;
    }

    filas = fila;
    columnas = ancho_detectado;

    if (filas <= 0 || columnas <= 0) {
        escribir_error("Error: mapa vacio o invalido\n");
        return 0;
    }

    if (cantidad_pacman == 0) {
        escribir_error("Error: no se encontro Pac-Man en el mapa\n");
        return 0;
    }

    if (cantidad_pacman > 1) {
        escribir_error("Error: hay mas de un Pac-Man en el mapa\n");
        return 0;
    }

    return 1;
}

void imprimir_mapa() {
    int i;
    int j;

    escribir_texto("\n=== MAPA ACTUAL ===\n");

    for (i = 0; i < filas; i++) {
        for (j = 0; j < columnas; j++) {
            syscall(SYS_write, 1, &mapa[i][j], 1);
        }

        escribir_texto("\n");
    }
}

void imprimir_posiciones() {
    int i;

    escribir_texto("\n=== POSICIONES ACTUALES ===\n");

    escribir_texto("Pac-Man: (");
    escribir_numero(pacman_x);
    escribir_texto(", ");
    escribir_numero(pacman_y);
    escribir_texto(")\n");

    for (i = 0; i < 4; i++) {
        escribir_texto("Fantasma ");
        escribir_numero(i + 1);
        escribir_texto(": (");
        escribir_numero(ghost_x[i]);
        escribir_texto(", ");
        escribir_numero(ghost_y[i]);
        escribir_texto(")\n");
    }

    escribir_texto("Vidas Pac-Man: ");
    escribir_numero(pacman_lives);
    escribir_texto("\n");
}

int dentro_del_mapa(int x, int y) {
    if (x < 0) {
        return 0;
    }

    if (y < 0) {
        return 0;
    }

    if (x >= columnas) {
        return 0;
    }

    if (y >= filas) {
        return 0;
    }

    return 1;
}