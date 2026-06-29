#ifndef MAP_H
#define MAP_H

#define MAX_FILAS 20
#define MAX_COLUMNAS 40
#define BUFFER_SIZE 4096

extern char mapa[MAX_FILAS][MAX_COLUMNAS];

extern int filas;
extern int columnas;

extern int pacman_x;
extern int pacman_y;
extern int pacman_lives;

extern int ghost_x[4];
extern int ghost_y[4];

void inicializar_mapa();
void revisar_personaje(char c, int fila, int columna);
int caracter_valido(char c);
int cargar_mapa(const char ruta_mapa[]);
void imprimir_mapa();
void imprimir_posiciones();
int dentro_del_mapa(int x, int y);

#endif