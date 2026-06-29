#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>

#define MAX_FILAS 20
#define MAX_COLUMNAS 40
#define BUFFER_SIZE 4096
#define RUTA_SIZE 200

char mapa[MAX_FILAS][MAX_COLUMNAS];

int filas = 0;
int columnas = 0;

int pacman_x = -1;
int pacman_y = -1;

int ghost_x[4];
int ghost_y[4];

/*
    Calcula la longitud de un texto.
    Esto sirve porque write necesita saber cuántos bytes imprimir.
*/
int longitud(const char texto[]) {
    int i = 0;

    while (texto[i] != '\0') {
        i++;
    }

    return i;
}

/*
    Imprime texto normal usando syscall write.
    1 = STDOUT, salida normal.
*/
void escribir_texto(const char texto[]) {
    syscall(SYS_write, 1, texto, longitud(texto));
}

/*
    Imprime errores usando syscall write.
    2 = STDERR, salida de errores.
*/
void escribir_error(const char texto[]) {
    syscall(SYS_write, 2, texto, longitud(texto));
}

/*
    Imprime un número entero usando write.
    No usamos printf para practicar syscalls.
*/
void escribir_numero(int numero) {
    char temp[20];
    char salida[20];
    int i = 0;
    int j = 0;

    if (numero == 0) {
        char cero = '0';
        syscall(SYS_write, 1, &cero, 1);
        return;
    }

    if (numero < 0) {
        char menos = '-';
        syscall(SYS_write, 1, &menos, 1);
        numero = numero * -1;
    }

    while (numero > 0) {
        temp[i] = (numero % 10) + '0';
        numero = numero / 10;
        i++;
    }

    while (i > 0) {
        i--;
        salida[j] = temp[i];
        j++;
    }

    syscall(SYS_write, 1, salida, j);
}

/*
    Construye una ruta así:

    carpeta = Caso1
    archivo = map.txt

    resultado = Caso1/map.txt
*/
int construir_ruta(const char carpeta[], const char archivo[], char ruta[]) {
    int i = 0;
    int j = 0;

    while (carpeta[i] != '\0' && j < RUTA_SIZE - 1) {
        ruta[j] = carpeta[i];
        i++;
        j++;
    }

    if (j > 0 && ruta[j - 1] != '/') {
        if (j >= RUTA_SIZE - 1) {
            return 0;
        }

        ruta[j] = '/';
        j++;
    }

    i = 0;

    while (archivo[i] != '\0' && j < RUTA_SIZE - 1) {
        ruta[j] = archivo[i];
        i++;
        j++;
    }

    ruta[j] = '\0';

    return 1;
}

/*
    Inicializa el mapa y las posiciones.
*/
void inicializar_mapa() {
    int i;
    int j;

    filas = 0;
    columnas = 0;

    pacman_x = -1;
    pacman_y = -1;

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

/*
    Revisa si el caracter leído es Pac-Man o un fantasma.
*/
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

/*
    Verifica si el caracter del mapa es válido.
*/
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

/*
    CHECKPOINT 1:
    Lee map.txt usando syscall open, read y close.
*/
int cargar_mapa(const char ruta_mapa[]) {
    char buffer[BUFFER_SIZE];

    int fd = syscall(SYS_open, ruta_mapa, O_RDONLY);

    if (fd < 0) {
        escribir_error("Error: no se pudo abrir el archivo map.txt\n");
        escribir_error("Ruta usada: ");
        escribir_error(ruta_mapa);
        escribir_error("\n");
        return 0;
    }

    int bytes_leidos = syscall(SYS_read, fd, buffer, BUFFER_SIZE - 1);

    if (bytes_leidos <= 0) {
        escribir_error("Error: no se pudo leer map.txt\n");
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

        /*
            Ignora \r por si el archivo viene con formato Windows.
        */
        if (c == '\r') {
            i++;
            continue;
        }

        /*
            Si encontramos salto de línea, termina una fila.
        */
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

        /*
            Validar tamaño máximo.
        */
        if (fila >= MAX_FILAS || columna >= MAX_COLUMNAS) {
            escribir_error("Error: el mapa supera el tamaño permitido\n");
            return 0;
        }

        /*
            Validar caracteres.
        */
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

    /*
        Si el archivo no termina con salto de línea,
        igual contamos la última fila.
    */
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

/*
    Imprime el mapa cargado.
*/
void imprimir_mapa() {
    int i;
    int j;

    escribir_texto("\n=== MAPA CARGADO ===\n");

    for (i = 0; i < filas; i++) {
        for (j = 0; j < columnas; j++) {
            syscall(SYS_write, 1, &mapa[i][j], 1);
        }

        escribir_texto("\n");
    }
}

/*
    Imprime datos generales del mapa.
*/
void imprimir_resumen_mapa() {
    escribir_texto("\n=== RESUMEN DEL MAPA ===\n");

    escribir_texto("Filas: ");
    escribir_numero(filas);
    escribir_texto("\n");

    escribir_texto("Columnas: ");
    escribir_numero(columnas);
    escribir_texto("\n");
}

/*
    Imprime posiciones encontradas.
*/
void imprimir_posiciones() {
    int i;

    escribir_texto("\n=== POSICIONES ENCONTRADAS ===\n");

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
}

int main(int argc, char *argv[]) {
    char ruta_mapa[RUTA_SIZE];

    escribir_texto("=== CHECKPOINT 1 ===\n");
    escribir_texto("Lectura y validacion de map.txt\n");

    if (argc < 2) {
        escribir_error("\nUso correcto:\n");
        escribir_error("./checkpoint1 Caso1\n");
        escribir_error("./checkpoint1 Caso2\n");
        escribir_error("./checkpoint1 Caso3\n");
        return 1;
    }

    if (!construir_ruta(argv[1], "map.txt", ruta_mapa)) {
        escribir_error("Error: no se pudo construir la ruta del mapa\n");
        return 1;
    }

    escribir_texto("\nCaso seleccionado: ");
    escribir_texto(argv[1]);
    escribir_texto("\n");

    escribir_texto("Ruta del mapa: ");
    escribir_texto(ruta_mapa);
    escribir_texto("\n");

    inicializar_mapa();

    if (!cargar_mapa(ruta_mapa)) {
        escribir_error("\nCheckpoint 1 FALLIDO\n");
        return 1;
    }

    escribir_texto("\nCheckpoint 1 OK: map.txt fue leido correctamente\n");

    imprimir_resumen_mapa();
    imprimir_mapa();
    imprimir_posiciones();

    return 0;
}