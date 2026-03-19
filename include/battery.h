#ifndef BATTERY_H
#define BATTERY_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pines personalizados para ESP32-S3
#define USB_CONNECTED 15
#define BAT_HOOK 16
#define BAT_ADC_EN 17
#define BAT_ADC   18


// Prototipos
void initBatteryPins();
bool isUSBConnected();
int readBatteryADC();


// Instancias compartidas


#ifdef __cplusplus
}
#endif

#endif // BATTERY_H
