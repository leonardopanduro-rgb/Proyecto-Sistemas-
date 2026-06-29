#define _GNU_SOURCE

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAX_FILAS 20
#define MAX_COLUMNAS 40
#define NUM_FANTASMAS 4
#define BUFFER_SIZE 4096
#define RUTA_SIZE 200
#define ARCHIVO_CHECKPOINTS "checkpoints.txt"

char mapa[MAX_FILAS][MAX_COLUMNAS];

int filas = 0;
int columnas = 0;

int pacman_x = -1;
int pacman_y = -1;

int ghost_x[NUM_FANTASMAS];
int ghost_y[NUM_FANTASMAS];

static int longitud(const char texto[]) {
    int i = 0;

    while (texto[i] != '\0') {
        i++;
    }

    return i;
}

static int escribir_en_fd(int fd, const char texto[]) {
    int total = 0;
    int cantidad = longitud(texto);

    while (total < cantidad) {
        long escritos = syscall(SYS_write,
                                 fd,
                                 texto + total,
                                 (unsigned long)(cantidad - total));

        if (escritos <= 0) {
            return 0;
        }

        total += (int)escritos;
    }

    return 1;
}

static void escribir_texto(const char texto[]) {
    escribir_en_fd(STDOUT_FILENO, texto);
}

static void escribir_error(const char texto[]) {
    escribir_en_fd(STDERR_FILENO, texto);
}

static int escribir_numero_en_fd(int fd, int numero) {
    char salida[32];
    unsigned int valor;
    int inicio = 0;
    int fin = 0;

    if (numero < 0) {
        salida[fin++] = '-';
        valor = (unsigned int)(-(numero + 1)) + 1U;
        inicio = 1;
    } else {
        valor = (unsigned int)numero;
    }

    do {
        salida[fin++] = (char)('0' + (valor % 10U));
        valor /= 10U;
    } while (valor > 0U);

    for (int izquierda = inicio, derecha = fin - 1;
         izquierda < derecha;
         izquierda++, derecha--) {
        char temporal = salida[izquierda];
        salida[izquierda] = salida[derecha];
        salida[derecha] = temporal;
    }

    salida[fin] = '\0';
    return escribir_en_fd(fd, salida);
}

static void escribir_numero(int numero) {
    escribir_numero_en_fd(STDOUT_FILENO, numero);
}

static int construir_ruta(const char carpeta[],
                          const char archivo[],
                          char ruta[]) {
    int i = 0;
    int j = 0;

    while (carpeta[i] != '\0') {
        if (j >= RUTA_SIZE - 1) {
            return 0;
        }

        ruta[j++] = carpeta[i++];
    }

    if (j > 0 && ruta[j - 1] != '/') {
        if (j >= RUTA_SIZE - 1) {
            return 0;
        }

        ruta[j++] = '/';
    }

    i = 0;

    while (archivo[i] != '\0') {
        if (j >= RUTA_SIZE - 1) {
            return 0;
        }

        ruta[j++] = archivo[i++];
    }

    ruta[j] = '\0';
    return 1;
}

static void inicializar_mapa(void) {
    filas = 0;
    columnas = 0;
    pacman_x = -1;
    pacman_y = -1;

    for (int fila = 0; fila < MAX_FILAS; fila++) {
        for (int columna = 0; columna < MAX_COLUMNAS; columna++) {
            mapa[fila][columna] = ' ';
        }
    }

    for (int i = 0; i < NUM_FANTASMAS; i++) {
        ghost_x[i] = -1;
        ghost_y[i] = -1;
    }
}

static int indice_fantasma(char caracter) {
    if (caracter >= 'A' && caracter <= 'D') {
        return caracter - 'A';
    }

    return -1;
}

static int caracter_valido(char caracter) {
    return caracter == 'X' ||
           caracter == 'O' ||
           caracter == '*' ||
           caracter == 'P' ||
           indice_fantasma(caracter) >= 0;
}

static int cargar_mapa(const char ruta_mapa[]) {
    char buffer[BUFFER_SIZE];
    int cantidad_pacman = 0;
    int cantidad_fantasmas[NUM_FANTASMAS] = {0, 0, 0, 0};
    int fila = 0;
    int columna = 0;
    int ancho_detectado = -1;

    int fd = (int)syscall(SYS_openat,
                          AT_FDCWD,
                          ruta_mapa,
                          O_RDONLY,
                          0);

    if (fd < 0) {
        escribir_error("Error: no se pudo abrir map.txt\nRuta usada: ");
        escribir_error(ruta_mapa);
        escribir_error("\n");
        return 0;
    }

    long bytes_leidos = syscall(SYS_read,
                                fd,
                                buffer,
                                (unsigned long)(BUFFER_SIZE - 1));

    if (bytes_leidos <= 0) {
        escribir_error("Error: map.txt esta vacio o no pudo leerse\n");
        syscall(SYS_close, fd);
        return 0;
    }

    if (bytes_leidos == BUFFER_SIZE - 1) {
        char extra;
        long bytes_extra = syscall(SYS_read, fd, &extra, 1UL);

        if (bytes_extra > 0) {
            escribir_error("Error: map.txt supera el buffer permitido\n");
            syscall(SYS_close, fd);
            return 0;
        }
    }

    syscall(SYS_close, fd);

    for (long i = 0; i < bytes_leidos; i++) {
        char caracter = buffer[i];

        if (caracter == '\r') {
            continue;
        }

        if (caracter == '\n') {
            if (columna == 0) {
                escribir_error("Error: el mapa contiene una fila vacia\n");
                return 0;
            }

            if (ancho_detectado == -1) {
                ancho_detectado = columna;
            } else if (columna != ancho_detectado) {
                escribir_error("Error: las filas no tienen el mismo ancho\n");
                return 0;
            }

            fila++;
            columna = 0;
            continue;
        }

        if (fila >= MAX_FILAS || columna >= MAX_COLUMNAS) {
            escribir_error("Error: el mapa supera 20 filas o 40 columnas\n");
            return 0;
        }

        if (!caracter_valido(caracter)) {
            escribir_error("Error: caracter invalido en el mapa: ");
            syscall(SYS_write, STDERR_FILENO, &caracter, 1UL);
            escribir_error("\n");
            return 0;
        }

        mapa[fila][columna] = caracter;

        if (caracter == 'P') {
            cantidad_pacman++;
            pacman_x = columna;
            pacman_y = fila;
        } else {
            int fantasma = indice_fantasma(caracter);

            if (fantasma >= 0) {
                cantidad_fantasmas[fantasma]++;
                ghost_x[fantasma] = columna;
                ghost_y[fantasma] = fila;
            }
        }

        columna++;
    }

    if (columna > 0) {
        if (ancho_detectado == -1) {
            ancho_detectado = columna;
        } else if (columna != ancho_detectado) {
            escribir_error("Error: la ultima fila tiene un ancho diferente\n");
            return 0;
        }

        fila++;
    }

    filas = fila;
    columnas = ancho_detectado;

    if (filas <= 0 || columnas <= 0) {
        escribir_error("Error: mapa vacio o invalido\n");
        return 0;
    }

    if (cantidad_pacman != 1) {
        escribir_error("Error: el mapa debe contener exactamente un Pac-Man\n");
        return 0;
    }

    for (int i = 0; i < NUM_FANTASMAS; i++) {
        if (cantidad_fantasmas[i] != 1) {
            escribir_error("Error: debe existir exactamente un fantasma ");
            char etiqueta = (char)('A' + i);
            syscall(SYS_write, STDERR_FILENO, &etiqueta, 1UL);
            escribir_error("\n");
            return 0;
        }
    }

    return 1;
}

static void imprimir_mapa(void) {
    escribir_texto("\n=== MAPA CARGADO ===\n");

    for (int fila = 0; fila < filas; fila++) {
        syscall(SYS_write,
                STDOUT_FILENO,
                mapa[fila],
                (unsigned long)columnas);
        escribir_texto("\n");
    }
}

static void imprimir_resumen_mapa(void) {
    escribir_texto("\n=== RESUMEN DEL MAPA ===\nFilas: ");
    escribir_numero(filas);
    escribir_texto("\nColumnas: ");
    escribir_numero(columnas);
    escribir_texto("\n");
}

static void imprimir_posiciones(void) {
    escribir_texto("\n=== POSICIONES ENCONTRADAS ===\nPac-Man: (");
    escribir_numero(pacman_x);
    escribir_texto(", ");
    escribir_numero(pacman_y);
    escribir_texto(")\n");

    for (int i = 0; i < NUM_FANTASMAS; i++) {
        escribir_texto("Fantasma ");
        char etiqueta = (char)('A' + i);
        syscall(SYS_write, STDOUT_FILENO, &etiqueta, 1UL);
        escribir_texto(": (");
        escribir_numero(ghost_x[i]);
        escribir_texto(", ");
        escribir_numero(ghost_y[i]);
        escribir_texto(")\n");
    }
}

static int registrar_checkpoint(const char caso[], int resultado_ok) {
    int fd = (int)syscall(SYS_openat,
                          AT_FDCWD,
                          ARCHIVO_CHECKPOINTS,
                          O_WRONLY | O_CREAT | O_APPEND,
                          0644);

    if (fd < 0) {
        escribir_error("Error: no se pudo crear checkpoints.txt\n");
        return 0;
    }

    int ok = 1;
    ok = ok && escribir_en_fd(fd, "=== CHECKPOINT 1 ===\nCaso: ");
    ok = ok && escribir_en_fd(fd, caso);
    ok = ok && escribir_en_fd(fd, "\nEstado: ");
    ok = ok && escribir_en_fd(fd, resultado_ok ? "OK\n" : "FALLIDO\n");

    if (resultado_ok) {
        ok = ok && escribir_en_fd(fd, "Mapa: ");
        ok = ok && escribir_numero_en_fd(fd, filas);
        ok = ok && escribir_en_fd(fd, " filas x ");
        ok = ok && escribir_numero_en_fd(fd, columnas);
        ok = ok && escribir_en_fd(fd, " columnas\nPac-Man: (");
        ok = ok && escribir_numero_en_fd(fd, pacman_x);
        ok = ok && escribir_en_fd(fd, ", ");
        ok = ok && escribir_numero_en_fd(fd, pacman_y);
        ok = ok && escribir_en_fd(fd, ")\n");
    }

    ok = ok && escribir_en_fd(fd, "\n");

    if (syscall(SYS_close, fd) != 0) {
        ok = 0;
    }

    if (!ok) {
        escribir_error("Error: no se pudo escribir completamente el checkpoint\n");
    }

    return ok;
}

int main(int argc, char *argv[]) {
    char ruta_mapa[RUTA_SIZE];

    escribir_texto("=== CHECKPOINT 1 ===\n");
    escribir_texto("Lectura y validacion de map.txt\n");

    if (argc != 2) {
        escribir_error("\nUso correcto:\n");
        escribir_error("./pacman_checkpoint Caso1\n");
        escribir_error("./pacman_checkpoint Caso2\n");
        escribir_error("./pacman_checkpoint Caso3\n");
        return 1;
    }

    if (!construir_ruta(argv[1], "map.txt", ruta_mapa)) {
        escribir_error("Error: la ruta del mapa es demasiado larga\n");
        return 1;
    }

    escribir_texto("\nCaso seleccionado: ");
    escribir_texto(argv[1]);
    escribir_texto("\nRuta del mapa: ");
    escribir_texto(ruta_mapa);
    escribir_texto("\n");

    inicializar_mapa();

    int mapa_ok = cargar_mapa(ruta_mapa);

    if (!registrar_checkpoint(argv[1], mapa_ok)) {
        return 1;
    }

    if (!mapa_ok) {
        escribir_error("\nCheckpoint 1 FALLIDO\n");
        return 1;
    }

    escribir_texto("\nCheckpoint 1 OK: map.txt fue leido correctamente\n");
    escribir_texto("Resultado guardado en checkpoints.txt\n");
    imprimir_resumen_mapa();
    imprimir_mapa();
    imprimir_posiciones();

    return 0;
}
