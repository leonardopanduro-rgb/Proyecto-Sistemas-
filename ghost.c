#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>

#include "ghost.h"
#include "map.h"
#include "utils.h"
#include "collision.h"

#define GHOST_MOVE_SIZE 2048

void limpiar_movimiento_fantasma(char movimiento[]) {
    int i = 0;

    while (movimiento[i] != '\0') {
        if (movimiento[i] == ' ') {
            movimiento[i] = '\0';
            return;
        }

        if (movimiento[i] == '\t') {
            movimiento[i] = '\0';
            return;
        }

        i++;
    }
}

void escribir_letra_fantasma(int id_fantasma, int x, int y) {
    if (id_fantasma == 0) {
        mapa[y][x] = 'A';
    } else if (id_fantasma == 1) {
        mapa[y][x] = 'B';
    } else if (id_fantasma == 2) {
        mapa[y][x] = 'C';
    } else if (id_fantasma == 3) {
        mapa[y][x] = 'D';
    }
}

void ejecutar_movimiento_fantasma(int id_fantasma, const char movimiento_original[]) {
    char movimiento[40];
    int k = 0;

    while (movimiento_original[k] != '\0' && k < 39) {
        movimiento[k] = movimiento_original[k];
        k++;
    }

    movimiento[k] = '\0';

    limpiar_movimiento_fantasma(movimiento);

    int actual_x = ghost_x[id_fantasma];
    int actual_y = ghost_y[id_fantasma];

    int nuevo_x = actual_x;
    int nuevo_y = actual_y;

    escribir_texto("\nMovimiento Fantasma ");
    escribir_numero(id_fantasma + 1);
    escribir_texto(": ");
    escribir_texto(movimiento);
    escribir_texto("\n");

    if (actual_x == -1 || actual_y == -1) {
        escribir_texto("Error: fantasma no encontrado en el mapa\n");
        return;
    }

    if (textos_iguales(movimiento, "UP")) {
        nuevo_y--;
    } else if (textos_iguales(movimiento, "DOWN")) {
        nuevo_y++;
    } else if (textos_iguales(movimiento, "LEFT")) {
        nuevo_x--;
    } else if (textos_iguales(movimiento, "RIGHT")) {
        nuevo_x++;
    } else {
        escribir_texto("Movimiento de fantasma no reconocido. Se ignora.\n");
        return;
    }

    if (!dentro_del_mapa(nuevo_x, nuevo_y)) {
        escribir_texto("Movimiento invalido: fuera del mapa\n");
        return;
    }

    if (mapa[nuevo_y][nuevo_x] == 'X') {
        escribir_texto("Movimiento invalido: hay pared\n");
        return;
    }

    mapa[actual_y][actual_x] = 'O';

    ghost_x[id_fantasma] = nuevo_x;
    ghost_y[id_fantasma] = nuevo_y;

    escribir_letra_fantasma(id_fantasma, nuevo_x, nuevo_y);

    escribir_texto("Movimiento valido. Nueva posicion Fantasma ");
    escribir_numero(id_fantasma + 1);
    escribir_texto(": (");
    escribir_numero(ghost_x[id_fantasma]);
    escribir_texto(", ");
    escribir_numero(ghost_y[id_fantasma]);
    escribir_texto(")\n");

    verificar_colision();
}

int ejecutar_movimientos_fantasma_desde_archivo(int id_fantasma, const char ruta_moves[]) {
    char buffer[GHOST_MOVE_SIZE];
    char movimiento[40];

    int fd = syscall(SYS_open, ruta_moves, O_RDONLY);

    if (fd < 0) {
        escribir_error("Error: no se pudo abrir archivo de movimientos del fantasma\n");
        escribir_error("Ruta usada: ");
        escribir_error(ruta_moves);
        escribir_error("\n");
        return 0;
    }

    int bytes_leidos = syscall(SYS_read, fd, buffer, GHOST_MOVE_SIZE - 1);

    if (bytes_leidos <= 0) {
        escribir_error("Error: no se pudo leer archivo de movimientos del fantasma\n");
        syscall(SYS_close, fd);
        return 0;
    }

    syscall(SYS_close, fd);

    int i = 0;
    int j = 0;

    while (i < bytes_leidos) {
        char c = buffer[i];

        if (c == '\r') {
            i++;
            continue;
        }

        if (c == '\n') {
            movimiento[j] = '\0';

            if (j > 0) {
                ejecutar_movimiento_fantasma(id_fantasma, movimiento);
                imprimir_mapa();
                imprimir_posiciones();
            }

            j = 0;
            i++;
            continue;
        }

        if (j < 39) {
            movimiento[j] = c;
            j++;
        }

        i++;
    }

    if (j > 0) {
        movimiento[j] = '\0';
        ejecutar_movimiento_fantasma(id_fantasma, movimiento);
        imprimir_mapa();
        imprimir_posiciones();
    }

    return 1;
}