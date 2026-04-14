#include "dw1000_app.h"
#include "config.h"

#include "DW1000Ranging.h"

#include <cstring>

// Adaptación a modo máximo alcance: modo largo, PRF 64MHz, baja tasa
static const byte DW1000_MAX_RANGE_MODE[] = {DW1000Class::TRX_RATE_110KBPS, DW1000Class::TX_PULSE_FREQ_64MHZ, DW1000Class::TX_PREAMBLE_LEN_2048};
static const uint8_t DW1000_INIT_MAX_ATTEMPTS = 10;
static const uint16_t DW1000_POWER_CYCLE_DELAY_MS = 50;
static const uint16_t DW1000_BOOT_SETTLE_MS = 1000;
static const uint32_t DW1000_DIAG_REPORT_INTERVAL_MS = 2000;

static bool g_dw1000Ready = false;

struct AnchorRangeEntry {
    bool active;
    uint16_t shortAddress;
    float distanceMeters;
    uint32_t lastUpdateMs;
};

static AnchorRangeEntry g_anchorRanges[UWB_MAX_ACTIVE_ANCHORS] = {};
static bool g_anchorTableDirty = false;
static uint32_t g_lastAnchorReportMs = 0;
static uint32_t g_lastRemoteSeenMs = 0;
static uint32_t g_prueba = 0;
static uint32_t g_lastBlinkRxMs = 0;
static uint32_t g_blinkRxCount = 0;
static uint32_t g_lastDiagReportMs = 0;

static const char* localRoleLabel() {
#if UWB_ROLE_ANCHOR
    return "ANCHOR";
#else
    return "TAG";
#endif
}

static const char* remoteRoleLabelSingular() {
#if UWB_ROLE_ANCHOR
    return "Tag";
#else
    return "Ancla";
#endif
}

static const char* remoteRoleLabelPlural() {
#if UWB_ROLE_ANCHOR
    return "Tags conectados";
#else
    return "Anclas activas";
#endif
}

static void applyRuntimeRadioSettings() {
    // `setAntennaDelay()` solo actualiza el valor en memoria; para que el
    // DW1000 lo use realmente hay que reescribir la configuración al chip.
    DW1000.newConfiguration();
    DW1000.useSmartPower(false);
    DW1000.setAntennaDelay(UWB_ACTIVE_ANTENNA_DELAY);
    DW1000.commitConfiguration();

    Serial.print("[DW1000 ");
    Serial.print(localRoleLabel());
    Serial.print("] Calibración de antena aplicada: ");
    Serial.println(UWB_ACTIVE_ANTENNA_DELAY);
}

static int findAnchorIndex(uint16_t shortAddress) {
    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (g_anchorRanges[i].active && g_anchorRanges[i].shortAddress == shortAddress) {
            return i;
        }
    }
    return -1;
}

static int findFreeAnchorSlot() {
    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (!g_anchorRanges[i].active) {
            return i;
        }
    }
    return -1;
}

static int findOldestAnchorSlot() {
    int oldestIndex = -1;
    uint32_t oldestTimestamp = UINT32_MAX;

    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (g_anchorRanges[i].active && g_anchorRanges[i].lastUpdateMs < oldestTimestamp) {
            oldestTimestamp = g_anchorRanges[i].lastUpdateMs;
            oldestIndex = i;
        }
    }
    // Fallback: if no active entry found, use slot 0
    return (oldestIndex >= 0) ? oldestIndex : 0;
}

static void upsertAnchorRange(uint16_t shortAddress, float distanceMeters) {
    int index = findAnchorIndex(shortAddress);
    if (index < 0) {
        index = findFreeAnchorSlot();
    }
    if (index < 0) {
        // Si se supera el limite configurado, reciclamos la entrada mas antigua.
        index = findOldestAnchorSlot();
    }

    g_anchorRanges[index].active = true;
    g_anchorRanges[index].shortAddress = shortAddress;
    g_anchorRanges[index].distanceMeters = distanceMeters;
    g_anchorRanges[index].lastUpdateMs = millis();
    g_anchorTableDirty = true;
}

static void markAnchorInactive(uint16_t shortAddress) {
    int index = findAnchorIndex(shortAddress);
    if (index >= 0) {
        g_anchorRanges[index].active = false;
        g_anchorTableDirty = true;
    }
}

static void touchAnchor(uint16_t shortAddress) {
    int index = findAnchorIndex(shortAddress);
    if (index >= 0) {
        g_anchorRanges[index].active = true;
        g_anchorRanges[index].lastUpdateMs = millis();
        g_anchorTableDirty = true;
        return;
    }

    upsertAnchorRange(shortAddress, 0.0f);
}


static void printDW1000DiagnosticsIfNeeded() {
#if UWB_ROLE_ANCHOR
    uint32_t now = millis();
    if (now - g_lastDiagReportMs < DW1000_DIAG_REPORT_INTERVAL_MS) {
        return;
    }
    g_lastDiagReportMs = now;

    uint32_t rxEvents = DW1000Ranging.getRxEventCount();
    uint32_t txEvents = DW1000Ranging.getTxEventCount();
    uint32_t lastRxMs = DW1000Ranging.getLastRxEventMs();
    uint32_t lastTxMs = DW1000Ranging.getLastTxEventMs();
    uint32_t sinceBlinkMs = (g_lastBlinkRxMs == 0) ? 0 : (now - g_lastBlinkRxMs);
    uint8_t activeTags = 0;
    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (g_anchorRanges[i].active) {
            ++activeTags;
        }
    }

    Serial.print("[DW1000 DIAG] irq=");
    Serial.print(digitalRead(DW1000_IRQ));
    Serial.print(" rxEvt=");
    Serial.print(rxEvents);
    Serial.print(" txEvt=");
    Serial.print(txEvents);
    Serial.print(" lastRxAgoMs=");
    Serial.print((lastRxMs == 0) ? 0 : (now - lastRxMs));
    Serial.print(" lastTxAgoMs=");
    Serial.print((lastTxMs == 0) ? 0 : (now - lastTxMs));
    Serial.print(" blinkCnt=");
    Serial.print(g_blinkRxCount);
    Serial.print(" blinkAgoMs=");
    Serial.print(sinceBlinkMs);
    Serial.print(" activeTags=");
    Serial.println(activeTags);
#endif
}
static void markRemoteSeenNow() {
    g_lastRemoteSeenMs = millis();
}

static void removeStaleAnchors() {
    uint32_t now = millis();
    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (!g_anchorRanges[i].active) {
            continue;
        }
        if (now - g_anchorRanges[i].lastUpdateMs > UWB_ANCHOR_STALE_TIMEOUT_MS) {
            g_anchorRanges[i].active = false;
            g_anchorTableDirty = true;
        }
    }
}

static uint8_t countActiveAnchors() {
    uint8_t count = 0;
    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (g_anchorRanges[i].active) {
            ++count;
        }
    }
    return count;
}

static void printAnchorRangesIfNeeded() {
    uint32_t now = millis();
    if (!g_anchorTableDirty || (now - g_lastAnchorReportMs < UWB_ANCHOR_REPORT_INTERVAL_MS)) {
        return;
    }

    g_lastAnchorReportMs = now;
    g_anchorTableDirty = false;

    uint8_t activeCount = countActiveAnchors();
    Serial.print("[DW1000 ");
    Serial.print(localRoleLabel());
    Serial.print("] ");
    Serial.print(remoteRoleLabelPlural());
    Serial.print(": ");
    Serial.println(activeCount);

    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (!g_anchorRanges[i].active) {
            continue;
        }

        Serial.print("  - 0x");
        Serial.print(g_anchorRanges[i].shortAddress, HEX);
        Serial.print(": ");
        Serial.print(g_anchorRanges[i].distanceMeters, 3);
        Serial.print(" m (hace ");
        Serial.print(now - g_anchorRanges[i].lastUpdateMs);
        Serial.println(" ms)");
    }
}

static void clearAnchorRanges() {
    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        g_anchorRanges[i].active = false;
        g_anchorRanges[i].shortAddress = 0;
        g_anchorRanges[i].distanceMeters = 0.0f;
        g_anchorRanges[i].lastUpdateMs = 0;
    }
    g_anchorTableDirty = true;
    g_lastAnchorReportMs = millis();
}

static bool isDW1000IdValid() {
    char msg[96];
    DW1000.getPrintableDeviceIdentifier(msg);
    SERIAL_LOG.print("ID:  ");
    SERIAL_LOG.println(msg);
    return std::strncmp(msg, "DECA", 4) == 0;
}

static void pulseDW1000Reset() {
    // RSTn must be released (floating) after the low pulse.
    digitalWrite(DW1000_RST, LOW);
	vTaskDelay(100 / portTICK_PERIOD_MS); // 100 ms
    digitalWrite(DW1000_RST, HIGH);
    vTaskDelay(100 / portTICK_PERIOD_MS); // 100 ms
}

static bool initDW1000CommunicationWithRetry() {
    // DW1000Ranging.initCommunication(DW1000_RST, DW1000_SS, DW1000_IRQ,
    // DW1000_SCK, DW1000_MISO, DW1000_MOSI, DW1000_SS);
    for (uint8_t attempt = 1; attempt <= DW1000_INIT_MAX_ATTEMPTS; ++attempt) {
        Serial.print("[DW1000][INIT] Intento ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(DW1000_INIT_MAX_ATTEMPTS);
        DW1000.end();
        
        detachInterrupt(digitalPinToInterrupt(DW1000_IRQ));
        desenergizeDW1000();
        vTaskDelay(DW1000_POWER_CYCLE_DELAY_MS / portTICK_PERIOD_MS); // 50 ms
        energizeDW1000();
        vTaskDelay(DW1000_BOOT_SETTLE_MS / portTICK_PERIOD_MS); // 1000 ms
	    vTaskDelay(500 / portTICK_PERIOD_MS); 


        pulseDW1000Reset();
        DW1000Ranging.initCommunication(DW1000_RST, DW1000_SS, DW1000_IRQ,
            DW1000_SCK, DW1000_MISO, DW1000_MOSI, DW1000_SS);
        vTaskDelay(500 / portTICK_PERIOD_MS); // 500 ms

        if (isDW1000IdValid()) {
            Serial.println("[DW1000][INIT] ID valida detectada");
            return true;
        }

        Serial.println("[DW1000][INIT] ID invalida. Reintentando...");
        DW1000.end();
    

    }
    desenergizeDW1000();
    ESP.restart();
    return false;
}

void runDW1000BootSelfTest() {
    char msg[96];

    Serial.println("[DW1000][SELFTEST] Inicio de autoprueba");

    DW1000.getPrintableDeviceIdentifier(msg);
    Serial.print("[DW1000][SELFTEST] Device ID: ");
    Serial.println(msg);

    DW1000.getPrintableExtendedUniqueIdentifier(msg);
    Serial.print("[DW1000][SELFTEST] EUI: ");
    Serial.println(msg);
    if(strcmp(msg, UWB_ADDRESS) != 0) {
        vTaskDelay(3000 / portTICK_PERIOD_MS); // 5 ms
        Serial.println("[DW1000][SELFTEST] Advertencia: EUI no coincide con la dirección "
            "configurada. Verifique UWB_ADDRESS en config.h");
    }
    DW1000.getPrintableNetworkIdAndShortAddress(msg);
    Serial.print("[DW1000][SELFTEST] Net/Short: ");
    Serial.println(msg);
    Serial.print("[DW1000][SELFTEST] IRQ nivel actual: ");
    Serial.println(digitalRead(DW1000_IRQ));
    Serial.println("[DW1000][SELFTEST] Fin de autoprueba");
}

static void handleNewDevice(DW1000Device* device) {
    if (device != nullptr) {
        touchAnchor(device->getShortAddress());
        markRemoteSeenNow();
        Serial.print("[DW1000 ");
        Serial.print(localRoleLabel());
        Serial.print("] ");
        Serial.print(remoteRoleLabelSingular());
        Serial.print(" detectado: 0x");
        Serial.println(device->getShortAddress(), HEX);
    }
}

static void handleBlinkDevice(DW1000Device* device) {
    if (device != nullptr) {
        touchAnchor(device->getShortAddress());
        markRemoteSeenNow();
        g_blinkRxCount++;
        g_lastBlinkRxMs = millis();
        Serial.print("[DW1000 ");
        Serial.print(localRoleLabel());
        Serial.print("] BLINK recibido de ");
        Serial.print(remoteRoleLabelSingular());
        Serial.print(": 0x");
        Serial.println(device->getShortAddress(), HEX);
    }
}

static void handleInactiveDevice(DW1000Device* device) {
    if (device != nullptr) {
        Serial.print("[DW1000 ");
        Serial.print(localRoleLabel());
        Serial.print("] ");
        Serial.print(remoteRoleLabelSingular());
        Serial.print(" inactivo: 0x");
        Serial.println(device->getShortAddress(), HEX);
        markAnchorInactive(device->getShortAddress());
    }
}

static void handleNewRange() {
    DW1000Device* device = DW1000Ranging.getDistantDevice();
    if (device != nullptr) {
        float range = device->getRange();
        // Descartar mediciones inválidas (negativas o excesivamente grandes)
        if (range >= 0.0f && range <= MAX_VALID_RANGE_METERS) {
            upsertAnchorRange(device->getShortAddress(), range);
            markRemoteSeenNow();
        }
    }
}

static void startDW1000AsConfiguredRole() {
#if UWB_ROLE_ANCHOR
    static char kAnchorAddress[] = UWB_ADDRESS;
    DW1000Ranging.startAsAnchor(kAnchorAddress, DW1000_MAX_RANGE_MODE, false);
    Serial.println("[DW1000] Rol activo: ANCHOR");
#else
    static char kTagAddress[] = UWB_ADDRESS;
    DW1000Ranging.startAsTag(kTagAddress, DW1000_MAX_RANGE_MODE, false);
    Serial.println("[DW1000] Rol activo: TAG");
#endif
}

void initializeDW1000() {
   if (!initDW1000CommunicationWithRetry()) {
        Serial.println("[DW1000][INIT] Error: no se pudo validar el ID del DW1000");
        g_dw1000Ready = false;
        return;
    }

    // Modo TAG (habilita request-response) + máxima cobertura
    DW1000Ranging.attachBlinkDevice(handleBlinkDevice);
    DW1000Ranging.attachNewDevice(handleNewDevice);
    DW1000Ranging.attachInactiveDevice(handleInactiveDevice);
    DW1000Ranging.attachNewRange(handleNewRange);
    startDW1000AsConfiguredRole();

    // Aplicar tuning al radio sobre la configuración ya cargada por
    // `startAsAnchor()` / `startAsTag()`. Esto sí programa en hardware el
    // `UWB_ACTIVE_ANTENNA_DELAY`, que era lo que faltaba en modo ancla.
    applyRuntimeRadioSettings();

#if UWB_ROLE_ANCHOR
    // Ensure anchor returns to permanent RX after runtime radio updates.
    DW1000Ranging.forceReceiverMode();
    Serial.println("[DW1000 ANCHOR] Modo recepcion rearmado");
#endif

    // Autoprueba de arranque para validar comunicacion con el DW1000.
    runDW1000BootSelfTest();

    // Asegurar actualización periódica
    DW1000Ranging.useRangeFilter(false);
    g_lastRemoteSeenMs = millis();
    g_lastBlinkRxMs = 0;
    g_blinkRxCount = 0;
    g_lastDiagReportMs = 0;
    g_dw1000Ready = true;
}

static void resetDW1000Module() {
    Serial.println("[DW1000][WATCHDOG] Sin remotos activos por 10s. Reiniciando modulo UWB...");

    g_dw1000Ready = false;
    clearAnchorRanges();
    desenergizeDW1000();
    vTaskDelay(DW1000_POWER_CYCLE_DELAY_MS / portTICK_PERIOD_MS); // 50 ms  
    energizeDW1000();
    vTaskDelay(DW1000_BOOT_SETTLE_MS / portTICK_PERIOD_MS); // 1000 ms

    initializeDW1000();
    Serial.println("[DW1000][WATCHDOG] Modulo UWB reiniciado correctamente");
}

void setupDW1000() {
    //Energizamos el módulo UWB
    Serial.println("[DW1000] Inicializando módulo UWB...");
    pinMode(UWB_EN, OUTPUT);
    desenergizeDW1000();

    // Pines de control ya definidos en config.h
    pinMode(DW1000_SS, OUTPUT);
    digitalWrite(DW1000_SS, HIGH); // Desactivar el chip select al inicio
    // RSTn should never be driven high by an external source. It should 
    // be driven low to reset the device, and then released (left floating) to 
    // allow the internal pull-up to bring it high.
    pinMode(DW1000_RST, OUTPUT_OPEN_DRAIN);
    digitalWrite(DW1000_RST,HIGH); // Aseguramos que el pin RSTn esté en HIGH 
    pinMode(DW1000_IRQ, INPUT);
    
    // Pines adicionales de control (WAKEUP y EXTON)
    // pinMode(DW1000_WAKEUP, OUTPUT);
    // digitalWrite(DW1000_WAKEUP, LOW);
    pinMode(DW1000_EXTON, INPUT);

    // Inicialización robusta del módulo con validación de ID y reintentos.s
	vTaskDelay(500 / portTICK_PERIOD_MS); // Esperamos a que el módulo se estabilice
    energizeDW1000();
    
    initializeDW1000();
}

void loopDW1000() {
    if (!g_dw1000Ready) {
        return;
    }

    DW1000.processInterrupt();
    DW1000Ranging.loop();
    removeStaleAnchors();

    // if (countActiveAnchors() == 0 && (millis() - g_lastRemoteSeenMs) >= UWB_NO_REMOTE_RESET_TIMEOUT_MS) {
    //     resetDW1000Module();
    //     return;
    // }

    if ( (millis() - g_prueba) >= 10000) {
        resetDW1000Module();
        g_prueba = millis();
        return;
    }

    // printDW1000DiagnosticsIfNeeded();
    printAnchorRangesIfNeeded();
}

void energizeDW1000() {
  digitalWrite(UWB_EN, HIGH); // Encendemos el módulo UWB
}

void desenergizeDW1000() {
  digitalWrite(UWB_EN, LOW); // Apagamos el módulo UWB
}

