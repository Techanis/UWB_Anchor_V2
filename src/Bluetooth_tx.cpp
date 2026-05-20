#include "Bluetooth_tx.h"
#include "wifi_tx.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <cstring>

// anchorMasterEnabled está definido en wifi_tx.cpp
extern bool isContinuous;
const String anchorPrefix = SENSOR_PREFIX;
// ---- Estado interno BLE ----
static BLEServer*         pServer      = nullptr;
static BLECharacteristic* pTxChar      = nullptr;
static bool               bleConnected = false;

// Staging de credenciales WiFi antes de confirmar con CMD:CONNECT
static char stagedSSID[MEMORY_MAX_SSID_LEN] = {};
static char stagedPass[MEMORY_MAX_PASS_LEN] = {};

// Cola de líneas recibidas, para procesar fuera del callback BLE (contexto ISR-safe)
static QueueHandle_t bleLineQueue = nullptr;
#define BLE_LINE_MAX_LEN 128
#define BLE_QUEUE_DEPTH   8

// Buffer acumulador de escrituras parciales del cliente
static String rxLineBuffer;

// ---- Helper: enviar respuesta por BLE notify + Serial ----
static void bleSend(const String& msg) {
    if (pTxChar && bleConnected) {
        pTxChar->setValue(msg.c_str());
        pTxChar->notify();
    }
    Serial.println("[BLE] " + msg);
}

// ---- Callbacks de conexión ----
class BLEConnectionCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pSvr) override {
        bleConnected = true;
        Serial.println("[BLE] Cliente conectado.");
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Pequeña pausa para evitar problemas de timing en la conexión
        bleSend("OK:CONTINUOUS:"+ String(isContinuous ? "1" : "0"));
    }
    void onDisconnect(BLEServer* pSvr) override {
        bleConnected = false;
        rxLineBuffer = "";
        Serial.println("[BLE] Cliente desconectado. Reiniciando advertising...");
        pSvr->startAdvertising();
    }
};

// ---- Callback RX: acumula bytes y encola líneas completas ----
class BLERxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        rxLineBuffer += String(val.c_str());
        int idx;
        while ((idx = rxLineBuffer.indexOf('\n')) >= 0) {
            String line = rxLineBuffer.substring(0, idx);
            line.trim();
            rxLineBuffer = rxLineBuffer.substring(idx + 1);
            if (line.length() == 0) continue;
            char buf[BLE_LINE_MAX_LEN];
            line.toCharArray(buf, sizeof(buf));
            if (bleLineQueue) xQueueSend(bleLineQueue, buf, 0);
        }
    }
};

static void formatId(char* buffer, size_t bufferSize) {
    unsigned int b1;
    if (sscanf(UWB_ADDRESS,
           "%*2X:%*2X:%*2X:%*2X:%*2X:%*2X:%*2X:%2X",
           &b1) == 1) {

        snprintf(buffer, bufferSize, "A-%s-%04X", anchorPrefix.c_str(), b1);
    } else {
        snprintf(buffer, bufferSize, "Anchor_Unknown");
    }
}


// ---- API pública ----

bool initBluetooth() {
    char anchorId[16];
    Serial.println("[BLE] Inicializando Bluetooth...");
    formatId(anchorId, sizeof(anchorId));
    if (bleLineQueue == nullptr) {
        bleLineQueue = xQueueCreate(BLE_QUEUE_DEPTH, BLE_LINE_MAX_LEN);
    }
    if (pServer != nullptr) {
        // Stack ya inicializado — limpiar buffer RX y rearmar advertising.
        rxLineBuffer = "";
        BLEDevice::startAdvertising();
        Serial.println("[BLE] Advertising reiniciado.");
        return true;
    }
    BLEDevice::init(anchorId);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new BLEConnectionCallbacks());

    BLEService* pService = pServer->createService(BLE_NUS_SERVICE_UUID);

    // TX: ESP envía al cliente (notify)
    pTxChar = pService->createCharacteristic(
        BLE_NUS_TX_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxChar->addDescriptor(new BLE2902());

    // RX: cliente envía al ESP (write)
    BLECharacteristic* pRxChar = pService->createCharacteristic(
        BLE_NUS_RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pRxChar->setCallbacks(new BLERxCallbacks());

    pService->start();

    // Potencia máxima TX para mayor alcance y visibilidad
    BLEDevice::setPower(ESP_PWR_LVL_P9);

    // Advertising: solo nombre en paquete principal; UUID 128-bit en scan response.
    // No mezclar ambos en el mismo paquete (flags+UUID128+nombre > 31 bytes → overflow).
    BLEAdvertisementData advData;
    advData.setFlags(0x06);          // LE General Discoverable | BR/EDR Not Supported
    advData.setName(anchorId);       // Complete Local Name en paquete principal

    BLEAdvertisementData scanRespData;
    scanRespData.setCompleteServices(BLEUUID(BLE_NUS_SERVICE_UUID));

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->setScanResponseData(scanRespData);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    String mac = BLEDevice::getAddress().toString().c_str();
    Serial.println("[BLE] Servidor NUS iniciado. MAC: " + mac);
    Serial.println("[BLE] Nombre: \"" + String(anchorId) + "\" | Usa UWB-config para encontrarlo.");
    return true;
}

void stopBluetooth() {
    BLEDevice::stopAdvertising();
    bleConnected = false;
    // No llamar deinit(): reinicializar el stack BT después de deinit(true)
    // bloquea esperando un semáforo de Bluedroid que nunca se señaliza.
    // Mantener el stack vivo y solo rearmar el advertising en initBluetooth().
    Serial.println("[BLE] Detenido.");
}

// Llama desde el loop de la tarea BLE para procesar los comandos encolados
bool processBLECommands() {
    char buf[BLE_LINE_MAX_LEN];
    bool hadCommands = false;
    while (bleLineQueue && xQueueReceive(bleLineQueue, buf, 0) == pdTRUE) {
        hadCommands = parseSerialCommand(String(buf)) || hadCommands;
    }
    return hadCommands;
}

// ---- Parser de comandos ----
// Protocolo: CMD:<KEY>[:<VALUE>]
//
// Comandos disponibles:
//   CMD:SSID:<valor>   → almacena SSID en staging (no escribe flash aún)
//   CMD:PASS:<valor>   → almacena contraseña en staging (no escribe flash aún)
//   CMD:CONNECT        → guarda staging en flash y conecta a WiFi
//   CMD:STATUS         → informa dirección UWB y estado WiFi
//   CMD:REBOOT         → reinicia el ESP
//
// Para agregar nuevos comandos: añadir un bloque else-if con key == "NUEVO_CMD"
bool parseSerialCommand(const String& line) {
    if (!line.startsWith("CMD:")) {
        bleSend("ERR:UNKNOWN_FORMAT");
        return false;
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

    Serial.printf("[BLE] CMD key=%s value=%s\n", key.c_str(), value.c_str());

    if (key == "WIFI_CONFIG") {
        int commaIndex = value.indexOf(',');

        if (value.length() == 0 || value.length() >= (MEMORY_MAX_SSID_LEN+MEMORY_MAX_PASS_LEN) 
            || commaIndex <= 0 || commaIndex >= value.length() - 1) {
            bleSend("ERR:WIFI_CONFIG_INVALID");
            return false;
        }
        String usuario = value.substring(0, commaIndex);
        String contrasena = value.substring(commaIndex + 1);
        usuario.toCharArray(stagedSSID, sizeof(stagedSSID));
        contrasena.toCharArray(stagedPass, sizeof(stagedPass));
        if (!writeWiFiCredentials(stagedSSID, stagedPass)) {
            bleSend("ERR:FLASH_WRITE_FAILED");
            return false;
        }
        bleSend("OK:WIFI_CONFIG_STAGED");

    } else if (key == "CONTINUOUS") {
        if (value.length() != 1) {
            bleSend("ERR:MODE_STAGED:_INVALID");
            return false;
        }
        isContinuous = value.equals("1");
        Serial.println(String("Modo continuo cambiado a: ") + (isContinuous ? "ON" : "OFF"));
        writeContinuousMode(isContinuous);
        bleSend("OK:CONTINUOUS:"+ String(isContinuous ? "1" : "0"));

    } else if (key == "STATUS_CONTINUOUS") {
        bleSend("OK:CONTINUOUS:"+ String(isContinuous ? "1" : "0"));

    } else if (key == "SSID") {
        if (value.length() == 0 || value.length() >= MEMORY_MAX_SSID_LEN) {
            bleSend("ERR:SSID_INVALID");
            return false;
        }
        value.toCharArray(stagedSSID, sizeof(stagedSSID));
        bleSend("OK:SSID_STAGED");

    } else if (key == "PASS") {
        if (value.length() >= MEMORY_MAX_PASS_LEN) {
            bleSend("ERR:PASS_TOO_LONG");
            return false;
        }
        value.toCharArray(stagedPass, sizeof(stagedPass));
        bleSend("OK:PASS_STAGED");

    } else if (key == "CONNECT") {
        if (stagedSSID[0] == '\0') {
            bleSend("ERR:NO_SSID_STAGED");
            return false;
        }
        eraseWiFiCredentials();
        if (!writeWiFiCredentials(stagedSSID, stagedPass)) {
            bleSend("ERR:FLASH_WRITE_FAILED");
            return false;
        }
        memset(stagedSSID, 0, sizeof(stagedSSID));
        memset(stagedPass,  0, sizeof(stagedPass));
        bleSend("OK:CREDENTIALS_SAVED");
        bleSend("OK:CONNECTING_WIFI");
        if (connectWiFi()) {
            bleSend("OK:WIFI_CONNECTED:" + WiFi.localIP().toString());
        } else {
            bleSend("ERR:WIFI_FAILED");
        }

    } else if (key == "STATUS") {
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        String msg  = "STATUS:ADDR=";
        msg += UWB_ADDRESS;
        msg += ":WIFI=";
        msg += wifiOk ? WiFi.localIP().toString() : "DISCONNECTED";
        bleSend(msg);

    } else if (key == "REBOOT") {
        bleSend("OK:REBOOTING");
        vTaskDelay(200 / portTICK_PERIOD_MS);
        ESP.restart();

    } else {
        // Punto de extensión: agregar nuevos comandos como bloques else-if antes de este else
        bleSend("ERR:UNKNOWN_CMD:" + key);
        return false;
    }
    return true;
}

