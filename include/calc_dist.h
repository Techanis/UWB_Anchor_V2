/*
Functions for calculating distance based on UWB data
Author: Ismael Takayama
Created: 2026-01-14


*/
#include "config.h"

#ifndef CALC_DIST_H
#define CALC_DIST_H
#define POSITION_UNKNOWN -1

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

// /**
//  * Calcula la mediana de un arreglo de enteros.
//  * @param datos Arreglo de entrada (se modificará el orden internamente en la copia).
//  * @param n Tamaño del arreglo.
//  * @return int El valor central (mediana) redondeado.
//  */

template <size_t COLS>
int calcularMediana(int (&datos)[COLS]) {
    if (COLS <= 0) return 0;

    // 1. Ordenamiento por Inserción (Eficiente para N pequeño en embebidos)
    for (int i = 1; i < COLS; i++) {
        int valorActual = datos[i];
        int j = i - 1;

        // Desplaza los elementos mayores hacia la derecha
        while (j >= 0 && datos[j] > valorActual) {
            datos[j + 1] = datos[j];
            j--;
        }
        datos[j + 1] = valorActual;
    }

    // 2. Selección de la mediana
    if (COLS % 2 != 0) {
        // Si el número de elementos es impar, tomamos el del centro
        return datos[COLS / 2];
    } else {
        // Si es par, promediamos los dos valores centrales y redondeamos
        // (Valor1 + Valor2 + 1) / 2 es un truco para redondear hacia arriba sin float
        return (datos[(COLS - 1) / 2] + datos[COLS / 2] + 1) / 2;
    }
}

#endif // CALC_DIST_H
