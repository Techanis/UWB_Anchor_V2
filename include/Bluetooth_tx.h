#ifndef BLUETOOTH_TX_H
#define BLUETOOTH_TX_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tiempo máximo esperando conexión BLE antes de pasar a ESP-NOW (ms)
#ifndef BLUETOOTH_TIME_WAIT_MS
#define BLUETOOTH_TIME_WAIT_MS 60000
#endif

// Nordic UART Service (NUS) — UUIDs estándar para serial sobre BLE
#define BLE_NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NUS_RX_CHAR_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // cliente → ESP
#define BLE_NUS_TX_CHAR_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP → cliente

// Prototipos
bool initBluetooth();
void stopBluetooth();
bool processBLECommands();
bool parseSerialCommand(const String& line);

#ifdef __cplusplus
}
#endif

#endif // BLUETOOTH_TX_H
