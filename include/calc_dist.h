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

// Utilidades ligeras de ordenamiento y estimación robusta para distancias UWB.
// Se basan en la misma lógica de inserción usada por `calcularMediana()`,
// pero ahora permiten trabajar con arreglos parciales y con `float`.

template <typename T>
static inline void ordenarPorInsercion(T* datos, size_t cantidad) {
    if (cantidad < 2U) {
        return;
    }
// 
    for (size_t i = 1; i < cantidad; ++i) {
        T valorActual = datos[i];
        size_t j = i;

        while (j > 0U && datos[j - 1U] > valorActual) {
            datos[j] = datos[j - 1U];
            --j;
        }
        datos[j] = valorActual;
    }
}

template <typename T>
static inline T calcularMedianaOrdenada(const T* datosOrdenados, size_t cantidad) {
    if (cantidad == 0U) {
        return static_cast<T>(0);
    }

    if ((cantidad % 2U) != 0U) {
        return datosOrdenados[cantidad / 2U];
    }

    return static_cast<T>((datosOrdenados[(cantidad - 1U) / 2U] + datosOrdenados[cantidad / 2U]) /
                          static_cast<T>(2));
}

template <typename T, size_t COLS>
T calcularMediana(const T (&datos)[COLS], size_t cantidadValidos = COLS) {
    if (cantidadValidos == 0U) {
        return static_cast<T>(0);
    }

    if (cantidadValidos > COLS) {
        cantidadValidos = COLS;
    }

    T copia[COLS];
    for (size_t i = 0; i < cantidadValidos; ++i) {
        copia[i] = datos[i];
    }

    ordenarPorInsercion(copia, cantidadValidos);
    return calcularMedianaOrdenada(copia, cantidadValidos);
}

template <typename T, size_t COLS>
T estimarDistanciaRobusta(const T (&datos)[COLS], size_t cantidadValidos = COLS) {
    if (cantidadValidos == 0U) {
        return static_cast<T>(0);
    }

    if (cantidadValidos > COLS) {
        cantidadValidos = COLS;
    }

    T copia[COLS];
    size_t cantidadUtil = 0U;

    for (size_t i = 0; i < cantidadValidos; ++i) {
        if (datos[i] > static_cast<T>(0)) {
            copia[cantidadUtil++] = datos[i];
        }
    }

    if (cantidadUtil == 0U) {
        return static_cast<T>(0);
    }

    ordenarPorInsercion(copia, cantidadUtil);
    const T mediana = calcularMedianaOrdenada(copia, cantidadUtil);

    if (cantidadUtil < 3U) {
        return mediana;
    }

    size_t descarteExtremos = (cantidadUtil >= 5U) ? 1U : 0U;
    if ((descarteExtremos * 2U) >= cantidadUtil) {
        descarteExtremos = 0U;
    }

    float sumaCentral = 0.0f;
    size_t cantidadCentral = 0U;
    for (size_t i = descarteExtremos; i < (cantidadUtil - descarteExtremos); ++i) {
        sumaCentral += static_cast<float>(copia[i]);
        ++cantidadCentral;
    }

    const float promedioRecortado =
        (cantidadCentral > 0U) ? (sumaCentral / static_cast<float>(cantidadCentral))
                               : static_cast<float>(mediana);

    // 70% mediana + 30% promedio recortado:
    // mantiene robustez ante outliers, pero suaviza variaciones entre rondas.
    return static_cast<T>(0.7f * static_cast<float>(mediana) +
                          0.3f * promedioRecortado);
}

#endif // CALC_DIST_H
