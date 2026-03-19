#include "dw1000_app.h"
#include "config.h"

#ifdef TAG
#undef TAG
#endif

#include "DW1000Ranging.h"

// Adaptación a modo máximo alcance: modo largo, PRF 64MHz, baja tasa
static const byte DW1000_MAX_RANGE_MODE[] = {DW1000Class::TRX_RATE_110KBPS, DW1000Class::TX_PULSE_FREQ_64MHZ, DW1000Class::TX_PREAMBLE_LEN_2048};

static volatile bool g_newRangeReady = false;
static float g_newRangeMeters = 0.0f;

static void handleNewRange() {
    DW1000Device* device = DW1000Ranging.getDistantDevice();
    if (device != nullptr) {
        g_newRangeMeters = device->getRange();
        g_newRangeReady = true;
    }
}

void setupDW1000() {
    // Configurar pines SPI con los valores definidos en config.h
    SPI.begin(DW1000_SCK, DW1000_MISO, DW1000_MOSI, DW1000_SS);
    
    // Pines de control ya definidos en config.h
    pinMode(DW1000_SS, OUTPUT);
    digitalWrite(DW1000_SS, HIGH);
    // RSTn should never be driven high by an external source. It should 
    // be driven low to reset the device, and then released (left floating) to 
    // allow the internal pull-up to bring it high.
    pinMode(DW1000_RST, INPUT);
    pinMode(DW1000_IRQ, INPUT);
    
    // Pines adicionales de control (WAKEUP y EXTON)
    pinMode(DW1000_WAKEUP, OUTPUT);
    digitalWrite(DW1000_WAKEUP, LOW);
    pinMode(DW1000_EXTON, INPUT);

    // Inicialización del módulo
    DW1000Ranging.initCommunication(DW1000_RST, DW1000_SS, DW1000_IRQ);

    // Modo TAG (habilita request-response) + máxima cobertura
    DW1000Ranging.attachNewRange(handleNewRange);
    DW1000Ranging.startAsTag("TAG00001", DW1000_MAX_RANGE_MODE, true);

    // Forzar potencia máxima de transmisión (esto deja de usar smart power)
    DW1000.useSmartPower(false);
    DW1000.setChannel(DW1000Class::CHANNEL_5);
    DW1000.setPreambleCode(DW1000Class::PREAMBLE_CODE_64MHZ_10);
    DW1000.enableMode(DW1000_MAX_RANGE_MODE);

    // Asegurar actualización periódica
    DW1000Ranging.useRangeFilter(false);
}

void loopDW1000() {
    DW1000Ranging.loop();

    if (g_newRangeReady) {
        g_newRangeReady = false;
        Serial.print("[DW1000 TAG] Distancia: ");
        Serial.print(g_newRangeMeters, 3);
        Serial.println(" m");
    }
}

