#include "utils.h"
#include "map.h"
#include "pacman.h"
#include "ghost.h"

int main(int argc, char *argv[]) {
    char ruta_mapa[RUTA_SIZE];
    char ruta_pacman_moves[RUTA_SIZE];
    char ruta_ghost_1_moves[RUTA_SIZE];

    escribir_texto("=== CHECKPOINT 1, 2 Y 3 ===\n");
    escribir_texto("Lectura de mapa, movimiento de Pac-Man y movimiento secuencial del Fantasma A\n");

    if (argc < 2) {
        escribir_error("Uso correcto:\n");
        escribir_error("./checkpoint3 Caso1\n");
        escribir_error("./checkpoint3 Caso2\n");
        escribir_error("./checkpoint3 Caso3\n");
        return 1;
    }

    escribir_texto("\nCaso seleccionado: ");
    escribir_texto(argv[1]);
    escribir_texto("\n");

    if (!construir_ruta(argv[1], "map.txt", ruta_mapa)) {
        escribir_error("Error construyendo ruta del mapa\n");
        return 1;
    }

    if (!construir_ruta(argv[1], "pacman_moves.txt", ruta_pacman_moves)) {
        escribir_error("Error construyendo ruta de movimientos de Pac-Man\n");
        return 1;
    }

    if (!construir_ruta(argv[1], "ghost_1_moves.txt", ruta_ghost_1_moves)) {
        escribir_error("Error construyendo ruta de movimientos del fantasma 1\n");
        return 1;
    }

    escribir_texto("Ruta mapa: ");
    escribir_texto(ruta_mapa);
    escribir_texto("\n");

    escribir_texto("Ruta movimientos Pac-Man: ");
    escribir_texto(ruta_pacman_moves);
    escribir_texto("\n");

    escribir_texto("Ruta movimientos Fantasma 1: ");
    escribir_texto(ruta_ghost_1_moves);
    escribir_texto("\n");

    inicializar_mapa();

    if (!cargar_mapa(ruta_mapa)) {
        escribir_error("No se pudo continuar por error en map.txt\n");
        return 1;
    }

    escribir_texto("\nCheckpoint 1 OK: mapa cargado correctamente\n");

    imprimir_mapa();
    imprimir_posiciones();

    escribir_texto("\nCheckpoint 2: ejecutando movimientos de Pac-Man\n");

    if (!ejecutar_movimientos_desde_archivo(ruta_pacman_moves)) {
        escribir_error("No se pudo continuar por error en pacman_moves.txt\n");
        return 1;
    }

    escribir_texto("\nCheckpoint 2 OK: Pac-Man se movio usando el archivo del caso\n");

    escribir_texto("\nCheckpoint 3: ejecutando movimientos del Fantasma A\n");

    if (!ejecutar_movimientos_fantasma_desde_archivo(0, ruta_ghost_1_moves)) {
        escribir_error("No se pudo continuar por error en ghost_1_moves.txt\n");
        return 1;
    }

    escribir_texto("\nCheckpoint 3 OK: Fantasma A se movio usando ghost_1_moves.txt\n");

    return 0;
}