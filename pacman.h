#ifndef PACMAN_H
#define PACMAN_H

void limpiar_movimiento(char movimiento[]);
void ejecutar_movimiento_pacman(const char movimiento_original[]);
int ejecutar_movimientos_desde_archivo(const char ruta_moves[]);

#endif