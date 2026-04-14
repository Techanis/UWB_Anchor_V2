#ifndef BATTERY_H
#define BATTERY_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Prototipos
void initBatteryPins();
bool isUSBConnected();
int readBatteryADC();
// Habilita o deshabilita la alimentación por batería (si no hay USB conectado)
void hookBattery(bool enable); 
// Instancias compartidas


#ifdef __cplusplus
}
#endif

#endif // BATTERY_H
