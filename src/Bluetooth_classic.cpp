// -----------------------------------------------------------------------
// Bluetooth_classic.cpp — Interfaz serial clásica (SPP) para configuración
// de credenciales WiFi. Implementa el mismo protocolo de comandos que
// Bluetooth_tx.cpp (BLE NUS), pero usando Bluetooth clásico (RFCOMM/SPP).
//
// Compatible con: ESP32 original, ESP32-WROVER.
// NO compatible con: ESP32-S2, ESP32-S3, ESP32-C3.
//
// Protocolo de comandos (idéntico al de BLE NUS):
//   CMD:SSID:<valor>   → almacena SSID en staging (no escribe flash aún)
//   CMD:PASS:<valor>   → almacena contraseña en staging (no escribe flash aún)
//   CMD:CONNECT        → guarda staging en flash y conecta a WiFi
//   CMD:STATUS         → informa dirección UWB y estado WiFi
//   CMD:REBOOT         → reinicia el ESP
//
// Para conectar desde el móvil:
//   Android: "Bluetooth Serial Terminal" o "Serial Bluetooth Terminal".
//   iOS: iOS no soporta SPP clásico — usar Bluetooth_tx.h (BLE NUS).
// -----------------------------------------------------------------------

#include "Bluetooth_classic.h"
#include "wifi_tx.h"
#include "memory.h"

#if defined(CONFIG_BT_CLASSIC_ENABLED)

#include <BluetoothSerial.h>

static BluetoothSerial SerialBT;
static bool  btClassicRunning = false;
static String btRxBuffer;

// Staging de credenciales WiFi antes de confirmar con CMD:CONNECT
static char btStagedSSID[MEMORY_MAX_SSID_LEN] = {};
static char btStagedPass[MEMORY_MAX_PASS_LEN] = {};

// ---- Helper: enviar respuesta por SPP + Serial ----
static void btSend(const String& msg) {
    if (btClassicRunning) {
        SerialBT.println(msg);
    }
    Serial.println("[BT] " + msg);
}

// ---- Parser de comandos ----
// Protocolo: CMD:<KEY>[:<VALUE>]
// Para agregar nuevos comandos: añadir un bloque else-if con key == "NUEVO_CMD"
static void btParseCommand(const String& line) {
    if (!line.startsWith("CMD:")) {
        btSend("ERR:UNKNOWN_FORMAT");
        return;
    }

    int firstColon = line.indexOf(':', 4);
    String key, value;
    if (firstColon < 0) {
        key = line.substring(4);
    } else {
        key   = line.substring(4, firstColon);
        value = line.substring(firstColon + 1);
        value.trim();
    }
    key.trim();

    Serial.printf("[BT] CMD key=%s value=%s\n", key.c_str(), value.c_str());

    if (key == "SSID") {
        if (value.length() == 0 || value.length() >= MEMORY_MAX_SSID_LEN) {
            btSend("ERR:SSID_INVALID");
            return;
        }
        value.toCharArray(btStagedSSID, sizeof(btStagedSSID));
        btSend("OK:SSID_STAGED");

    } else if (key == "PASS") {
        if (value.length() >= MEMORY_MAX_PASS_LEN) {
            btSend("ERR:PASS_TOO_LONG");
            return;
        }
        value.toCharArray(btStagedPass, sizeof(btStagedPass));
        btSend("OK:PASS_STAGED");

    } else if (key == "CONNECT") {
        if (btStagedSSID[0] == '\0') {
            btSend("ERR:NO_SSID_STAGED");
            return;
        }
        eraseWiFiCredentials();
        if (!writeWiFiCredentials(btStagedSSID, btStagedPass)) {
            btSend("ERR:FLASH_WRITE_FAILED");
            return;
        }
        memset(btStagedSSID, 0, sizeof(btStagedSSID));
        memset(btStagedPass,  0, sizeof(btStagedPass));
        btSend("OK:CREDENTIALS_SAVED");
        btSend("OK:CONNECTING_WIFI");
        if (connectWiFi()) {
            btSend("OK:WIFI_CONNECTED:" + WiFi.localIP().toString());
        } else {
            btSend("ERR:WIFI_FAILED");
        }

    } else if (key == "STATUS") {
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        String msg  = "STATUS:ADDR=";
        msg += UWB_ADDRESS;
        msg += ":WIFI=";
        msg += wifiOk ? WiFi.localIP().toString() : "DISCONNECTED";
        btSend(msg);

    } else if (key == "REBOOT") {
        btSend("OK:REBOOTING");
        vTaskDelay(200 / portTICK_PERIOD_MS);
        ESP.restart();

    } else {
        // Punto de extensión: agregar nuevos comandos como bloques else-if antes de este else
        btSend("ERR:UNKNOWN_CMD:" + key);
    }
}

// ---- API pública ----

bool initBluetoothClassic() {
    if (btClassicRunning) return true;

    if (!SerialBT.begin(BT_CLASSIC_DEVICE_NAME)) {
        Serial.println("[BT] Error al inicializar Bluetooth clásico.");
        return false;
    }

    btClassicRunning = true;
    btRxBuffer = "";
    Serial.println("[BT] Bluetooth clásico iniciado. Nombre: \"" BT_CLASSIC_DEVICE_NAME "\"");
    Serial.println("[BT] Emparejar desde Android con \"Bluetooth Serial Terminal\".");
    return true;
}

void stopBluetoothClassic() {
    if (!btClassicRunning) return;
    SerialBT.end();
    btClassicRunning = false;
    btRxBuffer = "";
    Serial.println("[BT] Bluetooth clásico detenido.");
}

// Llama desde el loop de la tarea para procesar datos recibidos.
// Acumula bytes y procesa líneas completas terminadas en '\n'.
void processBluetoothClassicCommands() {
    if (!btClassicRunning) return;

    while (SerialBT.available()) {
        char c = (char)SerialBT.read();
        if (c == '\r') continue;
        if (c == '\n') {
            btRxBuffer.trim();
            if (btRxBuffer.length() > 0) {
                btParseCommand(btRxBuffer);
            }
            btRxBuffer = "";
        } else {
            if (btRxBuffer.length() < 127) {
                btRxBuffer += c;
            }
        }
    }
}

#else // !CONFIG_BT_CLASSIC_ENABLED
// Stubs: Classic BT not available on this chip (e.g. ESP32-S3)
bool initBluetoothClassic()           { return false; }
void stopBluetoothClassic()           {}
void processBluetoothClassicCommands() {}
#endif // CONFIG_BT_CLASSIC_ENABLED
