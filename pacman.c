#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>

#include "pacman.h"
#include "map.h"
#include "utils.h"
#include "collision.h"

#define MOVE_SIZE 2048

void limpiar_movimiento(char movimiento[]) {
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

void ejecutar_movimiento_pacman(const char movimiento_original[]) {
    char movimiento[40];
    int k = 0;

    while (movimiento_original[k] != '\0' && k < 39) {
        movimiento[k] = movimiento_original[k];
        k++;
    }

    movimiento[k] = '\0';

    limpiar_movimiento(movimiento);

    int nuevo_x = pacman_x;
    int nuevo_y = pacman_y;

    escribir_texto("\nMovimiento Pac-Man: ");
    escribir_texto(movimiento);
    escribir_texto("\n");

    if (textos_iguales(movimiento, "UP")) {
        nuevo_y--;
    } else if (textos_iguales(movimiento, "DOWN")) {
        nuevo_y++;
    } else if (textos_iguales(movimiento, "LEFT")) {
        nuevo_x--;
    } else if (textos_iguales(movimiento, "RIGHT")) {
        nuevo_x++;
    } else {
        escribir_texto("Movimiento de Pac-Man no reconocido. Se ignora.\n");
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

    mapa[pacman_y][pacman_x] = 'O';

    pacman_x = nuevo_x;
    pacman_y = nuevo_y;

    mapa[pacman_y][pacman_x] = 'P';

    escribir_texto("Movimiento valido. Nueva posicion Pac-Man: (");
    escribir_numero(pacman_x);
    escribir_texto(", ");
    escribir_numero(pacman_y);
    escribir_texto(")\n");

    verificar_colision();
}

int ejecutar_movimientos_desde_archivo(const char ruta_moves[]) {
    char buffer[MOVE_SIZE];
    char movimiento[40];

    int fd = syscall(SYS_open, ruta_moves, O_RDONLY);

    if (fd < 0) {
        escribir_error("Error: no se pudo abrir pacman_moves.txt\n");
        escribir_error("Ruta usada: ");
        escribir_error(ruta_moves);
        escribir_error("\n");
        return 0;
    }

    int bytes_leidos = syscall(SYS_read, fd, buffer, MOVE_SIZE - 1);

    if (bytes_leidos <= 0) {
        escribir_error("Error: no se pudo leer pacman_moves.txt\n");
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
                ejecutar_movimiento_pacman(movimiento);
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
        ejecutar_movimiento_pacman(movimiento);
        imprimir_mapa();
        imprimir_posiciones();
    }

    return 1;
}