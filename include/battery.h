#ifndef BATTERY_H
#define BATTERY_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_MUESTRAS 32


// Prototipos
void initBatteryPins();
bool isUSBConnected();
int readBatteryADC();
// Habilita o deshabilita la alimentación por batería (si no hay USB conectado)
void hookBattery(bool enable); 
uint32_t readBatteryVoltageMv(); // Lee el voltaje de batería en mV
// Instancias compartidas


#ifdef __cplusplus
}
#endif

#endif // BATTERY_H
