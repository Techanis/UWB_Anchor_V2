#include "config.h"
#include "memory.h"
#include "battery.h"
#include "dw1000_app.h"

/// Global variables
String ssid;
String password;


// put function declarations here:
int myFunction(int, int);
bool connectWiFi();

void setup() {
  SERIAL_LOG.begin(115200);
  delay(5000); // Esperamos un segundo para asegurarnos de que el puerto serie esté listo

  setupDW1000();

  SERIAL_LOG.println(F("Iniciando dispositivo:"));
  pinMode(LED, OUTPUT);

  // Inicializamos pines de batería y alimentación

  Serial.println("Iniciando flash WiFi...");
  if (!initFlashWiFi()) 
    while (1) {
      delay(1000);
      Serial.println("Error inicializando flash.");
    } 
  initBatteryPins();
  Serial.println("Flash OK. ID: 0x" + String(flash.getJEDECID(), HEX));
  
  // Ejemplo: escribir credenciales (solo una vez)
  if (writeWiFiCredentials("MARGOT-2.4G", "7054201030J")) {
    SERIAL_LOG.println("Credenciales guardadas.");
  }
  
  // Conectar
  if (connectWiFi()) {
    Serial.print("Conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    SERIAL_LOG.println("Fallo en conexión WiFi.");
    SERIAL_LOG.println("Borrando credenciales para reintentar...");
    eraseWiFiCredentials();
  }

}

void loop() {
  loopDW1000();

  digitalWrite(LED, HIGH);
  //Esperamos un segundo
  delay(1000);
  //Apagamos el led
  digitalWrite(LED, LOW);
  if (digitalRead(DW1000_EXTON) == LOW) {
    SERIAL_LOG.println("DW1000 en modo de bajo consumo EXTON");
  } else {
    SERIAL_LOG.println("DW1000 activo EXTON");
  }

  if (digitalRead(DW1000_RST) == LOW) {
    SERIAL_LOG.println("DW1000 en modo de bajo consumo RST");
  } else {
    SERIAL_LOG.println("DW1000 activo RST");
  }
  SERIAL_LOG.print("Medición de batería: ");
  SERIAL_LOG.println(readBatteryADC());
  SERIAL_LOG.println(isUSBConnected() ? "Fuente USB conectada" : "Fuente USB no conectada");
  delay(1000);
}

// put function definitions here:
bool connectWiFi() {
  char ssid[MEMORY_MAX_SSID_LEN];
  char pass[MEMORY_MAX_PASS_LEN];
  
  if (!readWiFiCredentials(ssid, pass)) { 
    return false;
  }
  SERIAL_LOG.println("Iniciando conexión WiFi con SSID: " + String(ssid));
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    SERIAL_LOG.println(".");
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}