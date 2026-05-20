#include "memory.h"

SPIClass memorySPI; // default HSPI (o el que configure el core)
SPIFlash flash(FLASH_CS, &memorySPI);

void beginCustomSPI() {
  memorySPI.begin(FLASH_SCK, FLASH_MISO, FLASH_MOSI, FLASH_CS);
}

static bool writeStringToFlash(uint32_t addr, const char *str, size_t maxLen) {
  size_t len = strnlen(str, maxLen - 1) + 1;
  return flash.writeByteArray(addr, (uint8_t *)str, len);
}

static bool readStringFromFlash(uint32_t addr, char *buffer, size_t maxLen) {
  if (!flash.readByteArray(addr, (uint8_t *)buffer, maxLen)) {
    return false;
  }
  buffer[maxLen - 1] = '\0';
  return true;
}

bool initFlash() {
  beginCustomSPI();  // Configura pines antes de begin()
  
  if (!flash.begin(MB(128))) {
    return false;
  }

  Serial.println("Flash OK. ID: 0x" + String(flash.getJEDECID(), HEX));
  return true;
}

bool writeWiFiCredentials(const char *ssid, const char *pass) {
  char p_ssid[MEMORY_MAX_SSID_LEN] = {};
  char p_pass[MEMORY_MAX_PASS_LEN] = {};
  readWiFiCredentials(p_ssid, p_pass); // Lee primero para evitar sobreescribir con datos corruptos si ya hay algo escrito
  if (strcmp(p_ssid, ssid) != 0 || strcmp(p_pass, pass) != 0) {
    Serial.println("Actualizando credenciales WiFi en flash...");
  }
  else {
    Serial.println("Credenciales WiFi en flash ya están actualizadas.");
    return true; // No es necesario escribir si ya coinciden
  }
  eraseWiFiCredentials(); // Borramos antes para evitar dejar datos corruptos en caso de fallo de escritura
  return writeStringToFlash(ADDR_SSID, ssid, MEMORY_MAX_SSID_LEN) &&
         writeStringToFlash(ADDR_PASS, pass, MEMORY_MAX_PASS_LEN);
}

bool readWiFiCredentials(char *ssid, char *pass) {
  return readStringFromFlash(ADDR_SSID, ssid, MEMORY_MAX_SSID_LEN) &&
         readStringFromFlash(ADDR_PASS, pass, MEMORY_MAX_PASS_LEN);
}

bool eraseWiFiCredentials() {
  return flash.eraseSector(ADDR_SSID);
}

bool writeContinuousMode(bool isContinuous) {
  bool IsAlreadySet = false;
  if (readContinuousMode(IsAlreadySet) && IsAlreadySet == isContinuous) {
    Serial.println("Modo continuo en flash ya está actualizado.");
    return true; // No es necesario escribir si ya coincide
  }
  eraseContinuousMode(); // Borramos antes para evitar dejar datos corruptos en caso de fallo de escritura
  Serial.println("Actualizando modo continuo en flash...");
  return flash.writeByte(ADDR_CONTINUOUS, isContinuous ? 1 : 0);
}

bool readContinuousMode(bool &isContinuous) {
  uint8_t val;
  val=flash.readByte(ADDR_CONTINUOUS);
  isContinuous = (val != 0);
  return true;
}

bool eraseContinuousMode() {
  return flash.eraseSector(ADDR_CONTINUOUS);
}
// ── Almacenamiento genérico de registros en flash ──────────────────────────
// Diseño: cada slot ocupa (1 + recordSize) bytes.
//   Byte 0 : centinela READINGS_SENTINEL (0xAA) si el slot está ocupado, 0xFF si libre.
//   Bytes 1…recordSize : datos del registro.
// Al borrar (clearReadings) se borra por sectores completos, devolviendo todos
// los bytes a 0xFF.  saveReading siempre escribe en el primer slot libre.

static uint32_t readingOffset(uint16_t index, size_t recordSize) {
  return ADDR_READINGS_DATA + (uint32_t)index * (1u + recordSize);
}

uint16_t countReadings(size_t recordSize) {
  for (uint16_t i = 0; i < READINGS_MAX_COUNT; ++i) {
    if (flash.readByte(readingOffset(i, recordSize)) != READINGS_SENTINEL) {
      return i;
    }
  }
  return READINGS_MAX_COUNT;
}

bool saveReading(const uint8_t* data, size_t size) {
  const uint16_t count = countReadings(size);
  if (count >= READINGS_MAX_COUNT) {
    return false;
  }
  const uint32_t addr = readingOffset(count, size);
  if (!flash.writeByte(addr, READINGS_SENTINEL)) return false;
  return flash.writeByteArray(addr + 1u, const_cast<uint8_t*>(data), size);
}

bool loadReading(uint16_t index, uint8_t* buffer, size_t size) {
  const uint32_t addr = readingOffset(index, size);
  if (flash.readByte(addr) != READINGS_SENTINEL) {
    return false;
  }
  return flash.readByteArray(addr + 1u, buffer, size);
}

bool clearReadings(size_t recordSize) {
  const uint16_t count = countReadings(recordSize);
  const uint16_t sectorsToErase = (count * (1u + recordSize) + 4095u) / 4096u; // ceil()
  bool ok = true;
  for (uint8_t s = 0; s < sectorsToErase; ++s) {
    if (!flash.eraseSector(ADDR_READINGS_DATA + (uint32_t)s * 4096u)) {
      ok = false;
    }
  }
  return ok;
}

