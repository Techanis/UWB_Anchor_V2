#ifndef WIFI_TX_H
#define WIFI_TX_H

#include "config.h"
#include <WiFi.h>
#include <esp_now.h>

#ifdef __cplusplus
extern "C" {
#endif


// Servidores de datos
#define NTP_SERVER "pool.ntp.org"
#define DATA_SERVER "https://metrix-sensores-apu.onrender.com/api/sensors/readings/" // Cambia esto por tu servidor real
#define DATA_SERVER_API_KEY "084444f7af2e01fc6e8988c77d802fe4cc9df9babbf9865b1341a9c69c2f7b78" // API key incluida en el JSON del POST

// Zona horaria (GMT-5, Colombia)
#define gmtOffset_sec    (5 * -3600)
#define daylightOffset_sec 0

// Intervalo entre broadcasts de credenciales (ms)
#ifndef ESPNOW_BROADCAST_INTERVAL_MS
#define ESPNOW_BROADCAST_INTERVAL_MS 5000
#endif

// Intervalo entre saltos de canal en tags sin credenciales (ms)
// Debe ser > tiempo de recepción por canal. 500 ms da margen suficiente.
#ifndef ESPNOW_HOP_INTERVAL_MS
#define ESPNOW_HOP_INTERVAL_MS 500
#endif

extern bool     anchorMasterEnabled;
extern uint8_t  broadcastAddress[];

// Prototipos
bool initEspNow();
void stopEspNow();
bool loadCredentialsFromNVS(String &ssid, String &pass);
bool connectWiFi();
bool sendWiFiCredentials(String ssid, String password);
void periodicBroadcastCredentials();
bool startEspNow();
void processEspNowQueue();

// Estructura del paquete ESP-NOW
typedef struct {
    char     ssid[32];
    char     password[64];
    bool     is_Continuous; // Indica si el mensaje es para modo continuo o no
    uint32_t msg_id;  // ID aleatorio para identificar mensajes únicos
} wifi_credentials_t;

#ifdef __cplusplus
}
#endif

#endif // WIFI_TX_H
