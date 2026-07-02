#ifndef MAP_H
#define MAP_H

#include "shared.h"

int cargar_mapa(const char *ruta_mapa, SharedData *shared);
int es_celda_valida(SharedData *shared, int y, int x);
void imprimir_mapa(SharedData *shared);

#endif
