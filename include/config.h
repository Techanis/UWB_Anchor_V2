#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "time.h"

#ifdef __cplusplus
extern "C" {
#endif

// User config          ------------------------------------------
#ifndef UWB_ROLE_ANCHOR
#define UWB_ROLE_ANCHOR 01  // 0 = Tag, 1 = Anchor  (override via -D UWB_ROLE_ANCHOR=...)
#endif
#define UWB_TAG_COUNT 64
#define UWB_MAX_ACTIVE_ANCHORS 8
#ifndef UWB_ANCHOR_STALE_TIMEOUT_MS
  #ifdef DW1000_TAG_TX_PERIOD_S
    // Keep entries alive slightly longer than the TAG burst period so the
    // anchor does not mark them inactive between normal transmissions.
    #define UWB_ANCHOR_STALE_TIMEOUT_MS ((DW1000_TAG_TX_PERIOD_S * 1000UL) + 4000UL)
  #else
    #define UWB_ANCHOR_STALE_TIMEOUT_MS 12000
  #endif
#endif
#define UWB_ANCHOR_REPORT_INTERVAL_MS 1000
#ifndef UWB_NO_REMOTE_RESET_TIMEOUT_MS
#define UWB_NO_REMOTE_RESET_TIMEOUT_MS 10000
#endif
#define LED 42

// ---------------------- 
// Pines de DWM1000
#define DW1000_SS     10
#define DW1000_MOSI   11
#define DW1000_MISO   13
#define DW1000_SCK    12
// When asserted into its active high state, the WAKEUP pin brings
// the DW1000 out of SLEEP or DEEPSLEEP states into operational mode
#define DW1000_WAKEUP 5
// External device enable. Asserted during wake up process and held
// active until device enters sleep mode. 
#define DW1000_EXTON  6
#define DW1000_RST    4
#define DW1000_IRQ    9
#define UWB_EN        8

//-------------- 
// Pines de energía y batería 
#define USB_CONNECTED 15    
#define BAT_HOOK 16
#define BAT_ADC_EN 17
#define BAT_ADC   18

// WiFi credentials (will be stored in flash memory)
extern String ssid;               // Your WiFi network SSID
extern String password;         // Your WiFi network password      

// Dirección UWB de este nodo (override via -D UWB_ADDRESS=\"...\").
#ifndef UWB_ADDRESS
#define UWB_ADDRESS "AB:CD:12:41:FF:FF:FF:F1"
#endif

// Calibración de retardo de antena para el módulo DWM1000.
// El valor 16436 es un punto de partida razonable para PRF 64 MHz.
// Para calibración precisa: medir una distancia conocida y ajustar
// hasta que la distancia reportada coincida.
// Cada unidad ≈ 15.65 ps → 1 metro ≈ 64 unidades de ida y vuelta.
#ifndef UWB_ANTENNA_DELAY
#define UWB_ANTENNA_DELAY 16436
#endif

#ifndef UWB_ANTENNA_DELAY_TAG
#define UWB_ANTENNA_DELAY_TAG UWB_ANTENNA_DELAY
#endif

#ifndef UWB_ANTENNA_DELAY_ANCHOR
#define UWB_ANTENNA_DELAY_ANCHOR UWB_ANTENNA_DELAY
#endif

#if UWB_ROLE_ANCHOR
#define UWB_ACTIVE_ANTENNA_DELAY UWB_ANTENNA_DELAY_ANCHOR
#else
#define UWB_ACTIVE_ANTENNA_DELAY UWB_ANTENNA_DELAY_TAG
#endif

// Data de tiempo para sincronización (NTP)
# define gmtOffset_sec (5 * -3600) // GMT-5 (Colombia)
# define daylightOffset_sec 0

// Servidores de datos
#define NTP_SERVER "pool.ntp.org"
#define DATA_SERVER "http://example.com/data" // Cambia esto por tu servidor real

#define SERIAL_LOG Serial

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H