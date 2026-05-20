#include "dw1000_app.h"
#include "config.h"
#include "calc_dist.h"
#include "DW1000Ranging.h"

#include <cstring>


// 110 kbps, PRF 64 MHz, Preámbulo 4096 — máximo alcance / mínimo ruido de tiempo
// DW1000 UM: preamble ≥1024 sólo válido para 110 kbps.
// Airtime estimado @4096: SHR≈4310µs | PHR≈48µs | +73µs/byte
//   POLL_ACK(12B)≈5234µs  RANGE_REPORT(20B)≈5818µs  RANGING_INIT-master(22B)≈5964µs  BLINK(14B)≈5380µs
// (Con preámbulo 2048 los mismos frames eran ≈3150/3734/3880/3296 µs respectivamente)
static const byte DW1000_MAX_RANGE_MODE[] = {DW1000Class::TRX_RATE_110KBPS, DW1000Class::TX_PULSE_FREQ_64MHZ, DW1000Class::TX_PREAMBLE_LEN_4096};
static const uint8_t DW1000_INIT_MAX_ATTEMPTS = 10;
static const uint16_t DW1000_POWER_CYCLE_DELAY_MS = 2000;
static const uint16_t DW1000_BOOT_SETTLE_MS = 1000;
static const uint32_t DW1000_DIAG_REPORT_INTERVAL_MS = 2000;
static const uint8_t UWB_TOTAL_BURST_ROUNDS =
    DW1000_TAG_BURST_WARMUP_POLLS + DW1000_TAG_BURST_RANGING_ROUNDS;
static bool g_dw1000Ready = false;
const String anchorPrefix= SENSOR_PREFIX;

struct AnchorRangeEntry {
    bool active;
    uint16_t shortAddress;
    float distanceMeters;
    float medianDistanceMeters;
    float estimatedDistanceMeters;
    uint32_t lastUpdateMs;
    float roundSamples[UWB_TOTAL_BURST_ROUNDS];
    uint8_t sampleCount;
    uint8_t nextSampleIndex;
};

static AnchorRangeEntry g_anchorRanges[UWB_MAX_ACTIVE_ANCHORS] = {};
static bool g_anchorTableDirty = false;
static uint32_t g_lastAnchorReportMs = 0;
static uint32_t g_lastRemoteSeenMs = 0;
static uint32_t g_prueba = 0;
static uint32_t g_lastBlinkRxMs = 0;
static uint32_t g_blinkRxCount = 0;
static uint32_t g_lastDiagReportMs = 0;
static bool g_twdtSubscribed = false;

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

    Serial.printf("[DW1000 DIAG] irq=%d rxEvt=%lu txEvt=%lu lastRxAgoMs=%lu lastTxAgoMs=%lu blinkCnt=%lu blinkAgoMs=%lu activeTags=%u\n",
        digitalRead(DW1000_IRQ),
        (unsigned long)rxEvents,
        (unsigned long)txEvents,
        (unsigned long)((lastRxMs == 0) ? 0 : (now - lastRxMs)),
        (unsigned long)((lastTxMs == 0) ? 0 : (now - lastTxMs)),
        (unsigned long)g_blinkRxCount,
        (unsigned long)sinceBlinkMs,
        activeTags);
#endif
}
static void feedSystemWatchdog() {
    if (g_twdtSubscribed) {
        esp_task_wdt_reset();
    }
}

static void initSystemWatchdog() {
    // esp_task_wdt_init() acepta ser llamado cuando el TWDT ya está activo;
    // en ese caso solo actualiza el timeout y el flag de pánico.
    // El timeout es en segundos (API ESP-IDF v4.x).
    esp_task_wdt_init(UWB_CPU_WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL); // suscribe la tarea actual (Task1)
    g_twdtSubscribed = true;
    esp_task_wdt_reset();   // alimentación inicial
}

static void markRemoteSeenNow() {
    g_lastRemoteSeenMs = millis();
    feedSystemWatchdog();
}

static void applyRuntimeRadioSettings() {
    // `setAntennaDelay()` solo actualiza el valor en memoria; para que el
    // DW1000 lo use realmente hay que reescribir la configuración al chip.
    DW1000.newConfiguration();
    DW1000.useSmartPower(false);
    DW1000.setChannel(DW1000_CHANNEL);
    DW1000.setAntennaDelay(UWB_ACTIVE_ANTENNA_DELAY);

    // DW1000.setPreambleCode(DW1000Class::PREAMBLE_CODE_64MHZ_10);
    // DW1000.enableMode(DW1000_MAX_RANGE_MODE);

    DW1000.commitConfiguration();

    Serial.printf("[DW1000 %s] Calibración de antena aplicada: %u\n", localRoleLabel(), (unsigned)UWB_ACTIVE_ANTENNA_DELAY);
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

static int allocateAnchorSlot(uint16_t shortAddress) {
    int index = findAnchorIndex(shortAddress);
    if (index < 0) {
        index = findFreeAnchorSlot();
    }
    if (index < 0) {
        // Si se supera el limite configurado, reciclamos la entrada mas antigua.
        index = findOldestAnchorSlot();
    }
    return index;
}

static void updateAnchorDistanceEstimate(AnchorRangeEntry& entry) {
    entry.medianDistanceMeters = calcularMediana(entry.roundSamples, entry.sampleCount);
    entry.estimatedDistanceMeters = estimarDistanciaRobusta(entry.roundSamples, entry.sampleCount);
}

static void addSampleToAnchor(AnchorRangeEntry& entry, float distanceMeters) {
    entry.roundSamples[entry.nextSampleIndex] = distanceMeters;
    entry.nextSampleIndex = (entry.nextSampleIndex + 1U) % UWB_TOTAL_BURST_ROUNDS;
    if (entry.sampleCount < UWB_TOTAL_BURST_ROUNDS) {
        ++entry.sampleCount;
    }
    updateAnchorDistanceEstimate(entry);
}

static void formatNtpTimestamp(char* buffer, size_t bufferSize) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        snprintf(buffer, bufferSize, "1970-01-01T00:00:00");
        return;
    }

    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

static void upsertAnchorRange(uint16_t shortAddress, float distanceMeters) {
    const int index = allocateAnchorSlot(shortAddress);
    AnchorRangeEntry& entry = g_anchorRanges[index];

    if (!entry.active || entry.shortAddress != shortAddress) {
        entry = {};
        entry.shortAddress = shortAddress;
    }

    entry.active = true;
    entry.shortAddress = shortAddress;
    entry.distanceMeters = distanceMeters;
    entry.lastUpdateMs = millis();
    // Serial.println("[DW1000 TAG] Rango recibido: " + String(distanceMeters, 3) + " m de ancla 0x" + String(shortAddress, HEX));
    addSampleToAnchor(entry, distanceMeters);
    g_anchorTableDirty = true;
}

static void markAnchorInactive(uint16_t shortAddress) {
    int index = findAnchorIndex(shortAddress);
    if (index >= 0) {
        g_anchorRanges[index].active = false;
        g_anchorRanges[index].distanceMeters = 0.0f;
        g_anchorRanges[index].medianDistanceMeters = 0.0f;
        g_anchorRanges[index].estimatedDistanceMeters = 0.0f;
        g_anchorRanges[index].sampleCount = 0;
        g_anchorRanges[index].nextSampleIndex = 0;
        g_anchorTableDirty = true;
    }
}

static void touchAnchor(uint16_t shortAddress) {
    const int index = allocateAnchorSlot(shortAddress);
    AnchorRangeEntry& entry = g_anchorRanges[index];

    if (!entry.active || entry.shortAddress != shortAddress) {
        entry = {};
        entry.shortAddress = shortAddress;
    }
    
    entry.active = true;
    entry.lastUpdateMs = millis();
    g_anchorTableDirty = true;
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
    g_anchorTableDirty = false;

    uint8_t activeCount = countActiveAnchors();
    Serial.printf("[DW1000 %s] %s: %u\n", localRoleLabel(), remoteRoleLabelPlural(), activeCount);

    for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
        if (!g_anchorRanges[i].active) {
            continue;
        }
        Serial.printf("  - 0x%X: ult=%.3f m, mediana=%.3f m, estimada=%.3f m (%u/%u rondas, hace %lu ms)\n",
            g_anchorRanges[i].shortAddress,
            g_anchorRanges[i].distanceMeters,
            g_anchorRanges[i].medianDistanceMeters,
            g_anchorRanges[i].estimatedDistanceMeters,
            g_anchorRanges[i].sampleCount,
            (unsigned)UWB_TOTAL_BURST_ROUNDS,
            (unsigned long)(now - g_anchorRanges[i].lastUpdateMs));
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
    Serial.print("ID:  ");
    Serial.println(msg);
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

        desenergizeDW1000();
        vTaskDelay(DW1000_POWER_CYCLE_DELAY_MS / portTICK_PERIOD_MS); // 2000 ms
        energizeDW1000();
        vTaskDelay(DW1000_BOOT_SETTLE_MS / portTICK_PERIOD_MS); // 1000 ms

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
        Serial.printf("[DW1000 %s] %s detectado: 0x%X\n",
            localRoleLabel(), remoteRoleLabelSingular(), device->getShortAddress());
    }
}

static void handleBlinkDevice(DW1000Device* device) {
    if (device != nullptr) {
        touchAnchor(device->getShortAddress());
        markRemoteSeenNow();
        g_blinkRxCount++;
        g_lastBlinkRxMs = millis();
        Serial.printf("[DW1000 %s] BLINK recibido de %s: 0x%X\n",
            localRoleLabel(), remoteRoleLabelSingular(), device->getShortAddress());
    }
}

static void handleInactiveDevice(DW1000Device* device) {
    if (device != nullptr) {
        Serial.printf("[DW1000 %s] %s inactivo: 0x%X\n",
            localRoleLabel(), remoteRoleLabelSingular(), device->getShortAddress());
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
    // DW1000Ranging.useRangeFilter(false);
    g_lastRemoteSeenMs = millis();
    g_lastBlinkRxMs = 0;
    g_blinkRxCount = 0;
    g_lastDiagReportMs = 0;
    g_dw1000Ready = true;
}

static void resetDW1000Module() {
    Serial.println("[DW1000][WATCHDOG] Sin remotos activos. Reiniciando modulo UWB...");

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
    // Crear el semáforo binario del ISR del DW1000 una única vez antes de
    // que pueda dispararse la interrupción. Si se crea NULL, xSemaphoreGiveFromISR
    // hace un assert fatal.
    if (buttonSemaphore == NULL) {
        buttonSemaphore = xSemaphoreCreateBinary();
    }

    //Energizamos el módulo UWB
    Serial.println("[DW1000] Inicializando módulo UWB...");
    pinMode(UWB_EN, OUTPUT);
    desenergizeDW1000();
    vTaskDelay(3000 / portTICK_PERIOD_MS); // 100 ms
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
    pinMode(DW1000_WAKEUP, OUTPUT);
    digitalWrite(DW1000_WAKEUP, LOW);
    pinMode(DW1000_EXTON, INPUT);

    // Inicialización robusta del módulo con validación de ID y reintentos.s
	vTaskDelay(500 / portTICK_PERIOD_MS); // Esperamos a que el módulo se estabilice
    energizeDW1000();
    
    initializeDW1000();
}

void loopDW1000() {
    if (!g_twdtSubscribed) {
        initSystemWatchdog();
    }
    if (!g_dw1000Ready) {
        return;
    }
    // Serial.println("[DW1000] Loop principal ejecutándose...");
    if (xSemaphoreTake(buttonSemaphore,pdMS_TO_TICKS(1)) == pdTRUE) {
        DW1000.processInterrupt();
        DW1000Ranging.loop();
        removeStaleAnchors();
    } else{
        DW1000Ranging.loop();
        removeStaleAnchors();
    }

    // vTaskDelay(1/ portTICK_PERIOD_MS); // Evitar bloqueo total del loop
#if UWB_ROLE_ANCHOR
    if (g_anchorTableDirty) {
        bool shouldPrint = false;
        bool anyActive = false;
        bool allDone = true;
        uint32_t now = millis();
        for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
            if (!g_anchorRanges[i].active) continue;
            anyActive = true;
            // Este tag aún no terminó sus rondas y no ha agotado el timeout
            if (g_anchorRanges[i].sampleCount < UWB_TOTAL_BURST_ROUNDS &&
                now - g_anchorRanges[i].lastUpdateMs <= 500) {
                allDone = false;
            }
        }
        shouldPrint = anyActive && allDone;
        if (shouldPrint) {
            // printAnchorRangesIfNeeded();
            for (int i = 0; i < UWB_MAX_ACTIVE_ANCHORS; ++i) {
                if (g_anchorRanges[i].active) {
                    g_anchorRanges[i].sampleCount = 0;
                    g_anchorRanges[i].nextSampleIndex = 0;
                }
            }
        }
    }
#else
    static bool s_prevBurstActive = false;
    const bool burstActive = DW1000Ranging.isTagBurstActive();
    if (s_prevBurstActive && !burstActive) {
        Serial.println("[DW1000] Burst de TAG finalizado");
        // Burst recién completado: imprimir, enviar y/o guardar
        printAnchorRangesIfNeeded();
    }
    s_prevBurstActive = burstActive;
#endif
}

void energizeDW1000() {
  digitalWrite(UWB_EN, HIGH); // Encendemos el módulo UWB
}

void desenergizeDW1000() {
  digitalWrite(UWB_EN, LOW); // Apagamos el módulo UWB
}

