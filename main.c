#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>


#define MAX_FILAS 20
#define MAX_COLUMNAS 40
#define BUFFER_SIZE 4096
#define MOVE_SIZE 2048
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
*/
int longitud(const char texto[]) {
   int i = 0;


   while (texto[i] != '\0') {
       i++;
   }


   return i;
}


/*
   Imprime texto en pantalla usando syscall write.
*/
void escribir_texto(const char texto[]) {
   syscall(SYS_write, 1, texto, longitud(texto));
}


/*
   Imprime errores usando syscall write.
*/
void escribir_error(const char texto[]) {
   syscall(SYS_write, 2, texto, longitud(texto));
}


/*
   Imprime un número usando write.
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
   Construye una ruta como:
   Caso1 + "/" + map.txt


   Resultado:
   Caso1/map.txt
*/
int construir_ruta(const char carpeta[], const char archivo[], char ruta[]) {
   int i = 0;
   int j = 0;


   /*
       Copiar nombre de carpeta.
   */
   while (carpeta[i] != '\0' && j < RUTA_SIZE - 1) {
       ruta[j] = carpeta[i];
       i++;
       j++;
   }


   /*
       Agregar / si la carpeta no terminó con /.
   */
   if (j > 0 && ruta[j - 1] != '/') {
       if (j >= RUTA_SIZE - 1) {
           return 0;
       }


       ruta[j] = '/';
       j++;
   }


   /*
       Copiar nombre del archivo.
   */
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
   Guarda posición inicial de Pac-Man o fantasmas.
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
   Checkpoint 1:
   Lee el mapa desde la ruta recibida.
*/
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


   while (i < bytes_leidos) {
       char c = buffer[i];


       /*
           Ignorar \r por si el archivo viene de Windows.
       */
       if (c == '\r') {
           i++;
           continue;
       }


       /*
           Salto de línea: termina una fila.
       */
       if (c == '\n') {
           if (columna > 0) {
               if (ancho_detectado == -1) {
                   ancho_detectado = columna;
               } else {
                   if (columna != ancho_detectado) {
                       escribir_error("Advertencia: fila con tamaño diferente\n");
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


       mapa[fila][columna] = c;
       revisar_personaje(c, fila, columna);


       columna++;
       i++;
   }


   /*
       Si la última línea no terminó con \n.
   */
   if (columna > 0) {
       if (ancho_detectado == -1) {
           ancho_detectado = columna;
       }


       fila++;
   }


   filas = fila;
   columnas = ancho_detectado;


   if (pacman_x == -1 || pacman_y == -1) {
       escribir_error("Error: no se encontro Pac-Man en el mapa\n");
       return 0;
   }


   return 1;
}


/*
   Imprime el mapa.
*/
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


/*
   Imprime posiciones iniciales.
*/
void imprimir_posiciones() {
   int i;


   escribir_texto("\n=== POSICIONES INICIALES ===\n");


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


/*
   Compara dos textos.
*/
int textos_iguales(const char a[], const char b[]) {
   int i = 0;


   while (a[i] != '\0' && b[i] != '\0') {
       if (a[i] != b[i]) {
           return 0;
       }


       i++;
   }


   if (a[i] == '\0' && b[i] == '\0') {
       return 1;
   }


   return 0;
}


/*
   Verifica si una coordenada está dentro del mapa.
*/
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


/*
   Limpia espacios al final del movimiento.
   Sirve para casos como "DOWN ".
*/
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


/*
   Checkpoint 2:
   Ejecuta un movimiento de Pac-Man.
*/
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


   escribir_texto("\nMovimiento leido: ");
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
       escribir_texto("Movimiento no reconocido. Se ignora.\n");
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


   /*
       Actualizar mapa.
   */
   mapa[pacman_y][pacman_x] = 'O';


   pacman_x = nuevo_x;
   pacman_y = nuevo_y;


   mapa[pacman_y][pacman_x] = 'P';


   escribir_texto("Movimiento valido. Nueva posicion Pac-Man: (");
   escribir_numero(pacman_x);
   escribir_texto(", ");
   escribir_numero(pacman_y);
   escribir_texto(")\n");
}


/*
   Lee pacman_moves.txt desde la ruta del caso.
*/
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


   /*
       Procesar última línea si no termina en \n.
   */
   if (j > 0) {
       movimiento[j] = '\0';
       ejecutar_movimiento_pacman(movimiento);
       imprimir_mapa();
   }


   return 1;
}


int main(int argc, char *argv[]) {
   char ruta_mapa[RUTA_SIZE];
   char ruta_moves[RUTA_SIZE];


   escribir_texto("=== CHECKPOINT 1 Y 2 ===\n");


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

