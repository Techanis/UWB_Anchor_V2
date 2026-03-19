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

bool initFlashWiFi() {
  beginCustomSPI();  // Configura pines antes de begin()
  
  if (!flash.begin(MB(128))) {
    return false;
  }
  return true;
}

bool writeWiFiCredentials(const char *ssid, const char *pass) {
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

