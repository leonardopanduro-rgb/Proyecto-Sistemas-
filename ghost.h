#ifndef GHOST_H
#define GHOST_H

void limpiar_movimiento_fantasma(char movimiento[]);
void ejecutar_movimiento_fantasma(int id_fantasma, const char movimiento_original[]);
int ejecutar_movimientos_fantasma_desde_archivo(int id_fantasma, const char ruta_moves[]);

#endif
