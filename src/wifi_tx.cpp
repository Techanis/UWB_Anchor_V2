#include "wifi_tx.h"
#include <esp_wifi.h>

bool anchorMasterEnabled = DW1000_ANCHOR_MASTER_ENABLED;
// 2. Declarar el Handler de la cola de forma GLOBAL
QueueHandle_t  espNowQueue = NULL;
bool isContinuous = DW1000_CONTINUOUS_POST_MODE;
static wifi_credentials_t espnowData;
static esp_now_peer_info_t peerInfo;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Callbacks ESP-NOW ----

static void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    Serial.printf("[ESP-NOW] Broadcast %s\n",
                  status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

IRAM_ATTR static void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    wifi_credentials_t packet;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Verificar que la cola exista antes de intentar enviar datos
    if (espNowQueue != NULL && len <= sizeof(wifi_credentials_t)) {
        memcpy(&packet, data, sizeof(espnowData));
        
        // Enviar a la cola de forma segura desde la ISR
        xQueueSendFromISR(espNowQueue, &packet, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

void processEspNowQueue() {
    wifi_credentials_t packet;
    if (espNowQueue != NULL) {
        while (xQueueReceive(espNowQueue, &packet, 0) == pdTRUE) {
            Serial.printf("[ESP-NOW] Credenciales recibidas: ssid=%s\n", packet.ssid);
            Serial.printf("[ESP-NOW] Password: %s\n", packet.password);
    
            String ssid, password;
            if (!loadCredentialsFromNVS(ssid, password)) {
                Serial.println("[WiFi] No se pudieron cargar credenciales de flash.");
            }

            if(ssid != String(packet.ssid) || password != String(packet.password)) {
                Serial.println("[WiFi] Credenciales nuevas detectadas. Guardando en flash...");
                eraseWiFiCredentials();
                writeWiFiCredentials(packet.ssid, packet.password);
            } else {
                Serial.println("[WiFi] Credenciales recibidas son iguales a las guardadas. No se actualiza flash.");
            }
        } 
        Serial.println("[ESP-NOW] No hay datos en la cola para procesar.");
        
    }
}

// ---- API pública ----

void stopEspNow() {
    // Primero removemos el callback asignado
    esp_now_unregister_recv_cb(); 
    esp_now_deinit();
    Serial.println("[ESP-NOW] Detenido. WiFi listo para conectar.");
}

bool initEspNow() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[ESP-NOW] Deteniendo WiFi para iniciar ESP-NOW...");
        WiFi.disconnect();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init fallido");
        return false;
    }
     if (anchorMasterEnabled) {
        if (esp_now_is_peer_exist(broadcastAddress)) {
            Serial.println("El peer ya está registrado. Saltando paso...");
            return true;
        }
        esp_now_register_send_cb(onDataSent);
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.channel = 0;
        peerInfo.ifidx   = WIFI_IF_STA;
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.println("[ESP-NOW] No se pudo agregar peer");
            return false;
        } else {
            Serial.println("[ESP-NOW] Peer agregado para broadcast");
        }
    } else {
        esp_now_register_recv_cb(onDataRecv);
    }
    return true;
}

bool startEspNow() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[ESP-NOW] Deteniendo WiFi para iniciar ESP-NOW...");
        WiFi.disconnect();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init fallido");
        return false;
    }
    if (!initEspNow()) {
        Serial.println("[ESP-NOW] No se pudo iniciar ESP-NOW.");
        return false;
    }
    return true;
}

bool loadCredentialsFromNVS(String &ssid, String &pass) {
    char ssidbuf[MEMORY_MAX_SSID_LEN] = {};
    char passbuf[MEMORY_MAX_PASS_LEN] = {};
    if (!readWiFiCredentials(ssidbuf, passbuf) || ssidbuf[0] == '\0' || ssidbuf[0] == 0xFF) {
        return false;
    }
    ssid = String(ssidbuf);
    pass = String(passbuf);
    return true;
}

bool connectWiFi() {
    String ssid, password;
    if (!loadCredentialsFromNVS(ssid, password)) {
        Serial.println("[WiFi] No se pudieron cargar credenciales de flash.");
        return false;
    }
    WiFi.begin(ssid, password);
    Serial.printf("[WiFi] Conectando a %s", ssid.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        Serial.print(".");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("[WiFi] Conexión fallida.");
    return false;
}

bool sendWiFiCredentials(String ssid, String password) {
    if (!anchorMasterEnabled) return false;
    strncpy(espnowData.ssid,      ssid.c_str(),      sizeof(espnowData.ssid)     - 1);
    strncpy(espnowData.password,  password.c_str(),  sizeof(espnowData.password) - 1);
    espnowData.ssid[sizeof(espnowData.ssid) - 1]         = '\0';
    espnowData.password[sizeof(espnowData.password) - 1] = '\0';
    espnowData.msg_id = esp_random();
    espnowData.is_Continuous = isContinuous;
    return esp_now_send(broadcastAddress, (uint8_t *)&espnowData, sizeof(espnowData)) == ESP_OK;
}

// Llama esta función desde el loop del Task2 (solo master)
void periodicBroadcastCredentials() {
    static unsigned long lastBroadcast = 0;
    if (!anchorMasterEnabled) return;
    char ssidbuf[MEMORY_MAX_SSID_LEN] = {};
    char passbuf[MEMORY_MAX_PASS_LEN] = {};
    if (!readWiFiCredentials(ssidbuf, passbuf) || ssidbuf[0] == '\0' || ssidbuf[0] == 0xFF) {
        Serial.println("[ESP-NOW] No hay credenciales en flash para difundir.");
        return;
    }
    if (millis() - lastBroadcast >= ESPNOW_BROADCAST_INTERVAL_MS) {
        lastBroadcast = millis();
        sendWiFiCredentials(String(ssidbuf), String(passbuf));
    }
}