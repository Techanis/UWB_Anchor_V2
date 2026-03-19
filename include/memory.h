#ifndef MEMORY_H
#define MEMORY_H

#include "config.h"
#include <SPI.h>
#include <SPIMemory.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pines personalizados para ESP32-S3
#define FLASH_SCK  36
#define FLASH_MISO 21
#define FLASH_MOSI 35
#define FLASH_CS   14

// Dirección base en la flash externa
#define ADDR_SSID 0x0000u
#define ADDR_PASS 0x0100u

// Tamaños máximos almacenados (incluyendo '\0')
#define MEMORY_MAX_SSID_LEN 32
#define MEMORY_MAX_PASS_LEN 64

// Instancias compartidas
extern SPIClass memorySPI;
extern SPIFlash flash;

#ifdef __cplusplus
}
#endif

// Prototipos
bool initFlashWiFi();
bool writeWiFiCredentials(const char *ssid, const char *pass);
bool readWiFiCredentials(char *ssid, char *pass);
void beginCustomSPI();
bool eraseWiFiCredentials();

#endif // MEMORY_H
