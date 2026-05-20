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
#define ADDR_CONTINUOUS 0x1000u

// Almacenamiento de lecturas de ranging (modo no-continuo)
// Cada registro: 1 byte centinela (READINGS_SENTINEL) + sizeof(PostSnapshot) bytes de datos
// Sectores usados: READINGS_SECTOR_COUNT × 4096 bytes a partir de ADDR_READINGS_DATA
#define ADDR_READINGS_DATA    0x2000u   // Sector 2 (sector 0 reservado para credenciales WiFi, sector 1 para modo continuo)
#define READINGS_SENTINEL     0xAAu     // Marca de registro válido
#define READINGS_MAX_COUNT    18000u      // Máximo de snapshots almacenados
#define READINGS_SECTOR_COUNT 796u        // ceil(100 * (1 + ~180) / 4096) = 5 sectores

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
bool initFlash();
bool writeWiFiCredentials(const char *ssid, const char *pass);
bool readWiFiCredentials(char *ssid, char *pass);
void beginCustomSPI();
bool eraseWiFiCredentials();
bool writeContinuousMode(bool isContinuous);
bool readContinuousMode(bool &isContinuous);
bool eraseContinuousMode();

// Almacenamiento genérico de registros de tamaño fijo en flash
bool     saveReading(const uint8_t* data, size_t size);
uint16_t countReadings(size_t recordSize);
bool     loadReading(uint16_t index, uint8_t* buffer, size_t size);
bool     clearReadings(size_t recordSize);

#endif // MEMORY_H
