#include <unistd.h>
#include <sys/syscall.h>

#include "utils.h"

int longitud(const char texto[]) {
    int i = 0;

    while (texto[i] != '\0') {
        i++;
    }

    return i;
}

void escribir_texto(const char texto[]) {
    syscall(SYS_write, 1, texto, longitud(texto));
}

void escribir_error(const char texto[]) {
    syscall(SYS_write, 2, texto, longitud(texto));
}

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