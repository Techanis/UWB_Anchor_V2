#ifndef BLUETOOTH_CLASSIC_H
#define BLUETOOTH_CLASSIC_H

// -----------------------------------------------------------------------
// ADVERTENCIA: Bluetooth clásico (SPP) solo está disponible en el chip
// ESP32 original (ESP32, ESP32-WROVER, etc.).
// El ESP32-S2, ESP32-S3 y ESP32-C3 NO lo soportan.
// En chips sin CONFIG_BT_CLASSIC_ENABLED las funciones se compilan como stubs.
// -----------------------------------------------------------------------

#include "config.h"

// Nombre visible del dispositivo Bluetooth clásico
#ifndef BT_CLASSIC_DEVICE_NAME
  #define BT_CLASSIC_DEVICE_NAME "UWB-Anchor"
#endif

// Prototipos
bool initBluetoothClassic();
void stopBluetoothClassic();
void processBluetoothClassicCommands();

#endif // BLUETOOTH_CLASSIC_H
