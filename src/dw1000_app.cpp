#include "dw1000_app.h"
#include "config.h"

#include "DW1000Ranging.h"

// Adaptación a modo máximo alcance: modo largo, PRF 64MHz, baja tasa
static const byte DW1000_MAX_RANGE_MODE[] = {DW1000Class::TRX_RATE_110KBPS, DW1000Class::TX_PULSE_FREQ_64MHZ, DW1000Class::TX_PREAMBLE_LEN_2048};

static volatile bool g_newRangeReady = false;
static float g_newRangeMeters = 0.0f;
static uint16_t g_anchorShortAddress = 0;

static void runDW1000BootSelfTest() {
    char msg[96];

    Serial.println("[DW1000][SELFTEST] Inicio de autoprueba");

    DW1000.getPrintableDeviceIdentifier(msg);
    Serial.print("[DW1000][SELFTEST] Device ID: ");
    Serial.println(msg);

    DW1000.getPrintableExtendedUniqueIdentifier(msg);
    Serial.print("[DW1000][SELFTEST] EUI: ");
    Serial.println(msg);

    DW1000.getPrintableNetworkIdAndShortAddress(msg);
    Serial.print("[DW1000][SELFTEST] Net/Short: ");
    Serial.println(msg);

    Serial.print("[DW1000][SELFTEST] IRQ nivel actual: ");
    Serial.println(digitalRead(DW1000_IRQ));
    Serial.println("[DW1000][SELFTEST] Fin de autoprueba");
}

static void handleNewDevice(DW1000Device* device) {
    if (device != nullptr) {
        Serial.print("[DW1000] Dispositivo detectado: 0x");
        Serial.println(device->getShortAddress(), HEX);
    }
}

static void handleInactiveDevice(DW1000Device* device) {
    if (device != nullptr) {
        Serial.print("[DW1000] Dispositivo inactivo: 0x");
        Serial.println(device->getShortAddress(), HEX);
    }
}

static void handleNewRange() {
    DW1000Device* device = DW1000Ranging.getDistantDevice();
    if (device != nullptr) {
        g_newRangeMeters = device->getRange();
        g_anchorShortAddress = device->getShortAddress();
        g_newRangeReady = true;
    }
}

void setupDW1000() {
    //Energizamos el módulo UWB
    pinMode(UWB_EN, OUTPUT);
    energizeDW1000();

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

    // Modo UWB seleccionado por configuracion + maxima cobertura
    DW1000Ranging.attachNewDevice(handleNewDevice);
    DW1000Ranging.attachInactiveDevice(handleInactiveDevice);
    DW1000Ranging.attachNewRange(handleNewRange);
#if UWB_ROLE_ANCHOR
    static char kAnchorAddress[] = "ANCHOR01";
    DW1000Ranging.startAsAnchor(kAnchorAddress, DW1000_MAX_RANGE_MODE, true);
    Serial.println("[DW1000] Rol activo: ANCHOR");
#else
    static char kTagAddress[] = "TAG00001";
    DW1000Ranging.startAsTag(kTagAddress, DW1000_MAX_RANGE_MODE, true);
    Serial.println("[DW1000] Rol activo: TAG");
#endif

    // Forzar potencia máxima de transmisión (esto deja de usar smart power)
    DW1000.useSmartPower(false);
    DW1000.setChannel(DW1000Class::CHANNEL_5);
    DW1000.setPreambleCode(DW1000Class::PREAMBLE_CODE_64MHZ_10);
    DW1000.enableMode(DW1000_MAX_RANGE_MODE);

    // Autoprueba de arranque para validar comunicacion con el DW1000.
    runDW1000BootSelfTest();

    // Asegurar actualización periódica
    DW1000Ranging.useRangeFilter(false);
}

void loopDW1000() {
    DW1000Ranging.loop();
    SERIAL_LOG.println("DW1000 loop ejecutado.");
    if (g_newRangeReady) {
        g_newRangeReady = false;
        Serial.print("[DW1000] Distancia a 0x");
        Serial.print(g_anchorShortAddress, HEX);
        Serial.print(": ");
        Serial.print(g_newRangeMeters, 3);
        Serial.println(" m");
    }
}

void energizeDW1000() {
  digitalWrite(UWB_EN, HIGH); // Encendemos el módulo UWB
}

void desenergizeDW1000() {
  digitalWrite(UWB_EN, LOW); // Apagamos el módulo UWB
}