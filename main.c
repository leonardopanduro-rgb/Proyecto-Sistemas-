#include "utils.h"
#include "map.h"
#include "pacman.h"

int main(int argc, char *argv[]) {
    char ruta_mapa[RUTA_SIZE];
    char ruta_moves[RUTA_SIZE];

    escribir_texto("=== CHECKPOINT 1 Y 2 SEPARADO ===\n");

    if (argc < 2) {
        escribir_error("Uso correcto:\n");
        escribir_error("./checkpoint12 Caso1\n");
        escribir_error("./checkpoint12 Caso2\n");
        escribir_error("./checkpoint12 Caso3\n");
        return 1;
    }

    escribir_texto("Caso seleccionado: ");
    escribir_texto(argv[1]);
    escribir_texto("\n");

    if (!construir_ruta(argv[1], "map.txt", ruta_mapa)) {
        escribir_error("Error construyendo ruta del mapa\n");
        return 1;
    }

    if (!construir_ruta(argv[1], "pacman_moves.txt", ruta_moves)) {
        escribir_error("Error construyendo ruta de movimientos\n");
        return 1;
    }

    escribir_texto("Ruta mapa: ");
    escribir_texto(ruta_mapa);
    escribir_texto("\n");

    escribir_texto("Ruta movimientos: ");
    escribir_texto(ruta_moves);
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

    if (!ejecutar_movimientos_desde_archivo(ruta_moves)) {
        escribir_error("No se pudo continuar por error en pacman_moves.txt\n");
        return 1;
    }

    escribir_texto("\nCheckpoint 2 OK: Pac-Man se movio usando el archivo del caso\n");

    return 0;
}