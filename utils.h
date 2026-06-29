#ifndef UTILS_H
#define UTILS_H

#define RUTA_SIZE 200

int longitud(const char texto[]);
void escribir_texto(const char texto[]);
void escribir_error(const char texto[]);
void escribir_numero(int numero);
int construir_ruta(const char carpeta[], const char archivo[], char ruta[]);
int textos_iguales(const char a[], const char b[]);

#endif