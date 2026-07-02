#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "game.h"

/*
    Normaliza una instruccion en el mismo buffer.
    Elimina espacios, tabulaciones, '\n' y '\r' de ambos extremos. No usa
    memoria compartida y por tanto no necesita mutex.
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

    Retorna 1 para UP/DOWN/LEFT/RIGHT o SET_PRIORITY seguido de exactamente un
    entero; retorna 0 en cualquier otro caso. El rango se valida despues en P0,
    porque P1/P2 solo pueden solicitar y nunca aplicar una prioridad efectiva.
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
    Lee y valida una instruccion desde un FILE.

    Retorna LECTURA_OK, LECTURA_FIN, LECTURA_INVALIDA o LECTURA_ERROR. Las
    lineas vacias se ignoran. Detectar cada estado por separado permite que los
    productores publiquen EOF o error sin bloquear a sus consumidores.
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

/*
    Extrae el entero de SET_PRIORITY y lo escribe en nueva_prioridad.
    Retorna 1 si la instruccion tiene esa forma; no cambia SharedData.
*/
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

/* Retorna 1 cuando P0 puede aceptar la prioridad dentro del rango 1..100. */
int prioridad_valida(int prioridad) {
    return prioridad >= PRIORIDAD_MIN && prioridad <= PRIORIDAD_MAX;
}
