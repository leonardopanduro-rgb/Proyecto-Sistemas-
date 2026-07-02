#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "game.h"

/*
    Elimina espacios, tabulaciones, '\n' y '\r' de ambos extremos.
*/
void limpiar_espacios_movimiento(char movimiento[]) {
    int inicio = 0;
    int largo = strlen(movimiento);

    while (movimiento[inicio] != '\0' &&
           isspace((unsigned char)movimiento[inicio])) {
        inicio++;
    }

    while (largo > inicio &&
           isspace((unsigned char)movimiento[largo - 1])) {
        largo--;
    }

    int nuevo_largo = largo - inicio;

    if (inicio > 0 && nuevo_largo > 0) {
        memmove(movimiento, movimiento + inicio, nuevo_largo);
    }

    movimiento[nuevo_largo] = '\0';
}

/*
    Valida la forma de una instruccion sin ejecutarla.
    El rango de prioridad sigue siendo responsabilidad de P0.
*/
int instruccion_movimiento_valida(const char *movimiento) {
    if (strcmp(movimiento, "UP") == 0 ||
        strcmp(movimiento, "DOWN") == 0 ||
        strcmp(movimiento, "LEFT") == 0 ||
        strcmp(movimiento, "RIGHT") == 0) {
        return 1;
    }

    char comando[32];
    char sobrante[32];
    int valor;

    int elementos = sscanf(movimiento,
                           "%31s %d %31s",
                           comando,
                           &valor,
                           sobrante);

    return elementos == 2 && strcmp(comando, "SET_PRIORITY") == 0;
}

/*
    Retorna un estado diferente para instruccion valida, EOF y error.
    Las lineas vacias se ignoran y no terminan la lectura.
*/
int leer_movimiento(FILE *archivo, char movimiento[], int tam) {
    if (archivo == NULL) {
        return LECTURA_ERROR;
    }

    while (fgets(movimiento, tam, archivo) != NULL) {
        /*
            Si no entro el salto de linea y aun no llegamos a EOF,
            la instruccion supera el tamano permitido.
        */
        if (strchr(movimiento, '\n') == NULL && !feof(archivo)) {
            printf("[ERROR] Instruccion demasiado larga\n");
            return LECTURA_INVALIDA;
        }

        limpiar_espacios_movimiento(movimiento);

        if (movimiento[0] == '\0') {
            continue;
        }

        if (!instruccion_movimiento_valida(movimiento)) {
            printf("[ERROR] Instruccion de movimiento invalida: %s\n",
                   movimiento);
            return LECTURA_INVALIDA;
        }

        return LECTURA_OK;
    }

    if (ferror(archivo)) {
        return LECTURA_ERROR;
    }

    return LECTURA_FIN;
}

int extraer_prioridad(const char *movimiento, int *nueva_prioridad) {
    char comando[32];
    int valor;

    if (sscanf(movimiento, "%31s %d", comando, &valor) == 2) {
        if (strcmp(comando, "SET_PRIORITY") == 0) {
            *nueva_prioridad = valor;
            return 1;
        }
    }

    return 0;
}

int prioridad_valida(int prioridad) {
    return prioridad >= PRIORIDAD_MIN && prioridad <= PRIORIDAD_MAX;
}
