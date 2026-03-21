#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>


#ifdef __cplusplus
extern "C" {
#endif

// User config          ------------------------------------------
#define UWB_INDEX 2
// UWB role selection: 1 = Anchor, 0 = Tag
#define UWB_ROLE_ANCHOR 1
#define UWB_TAG_COUNT 64
#define LED 42

// DW1000 pin mapping (ajustar según tu hardware ESP32-S3)
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
// WiFi credentials (will be stored in flash memory)
extern String ssid;               // Your WiFi network SSID
extern String password;         // Your WiFi network password      

#define SERIAL_LOG Serial


extern HardwareSerial SERIAL_AT; 

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H