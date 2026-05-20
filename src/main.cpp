#include "config.h"
#include "battery.h"
#include "dw1000_app.h"
#include "wifi_tx.h"
#include "Bluetooth_tx.h"

// Declaración de tareas
TaskHandle_t DWM1000LoopTask;
TaskHandle_t Task2;

// Global variables
char ssid[MEMORY_MAX_SSID_LEN] = {};
char password[MEMORY_MAX_PASS_LEN] = {};
static volatile bool g_usbConnected = false; // true cuando voltaje USB >= 2600 mV
static const uint16_t USB_VOLTAGE_THRESHOLD_MV = 2600;
extern bool isContinuous; // Definida en Bluetooth_tx.cpp

// Declaración de funciones
void DWM1000Loop( void * pvParameters );
void Task2code( void * pvParameters );
bool getBluetoothData();
void usbConnectionLoop(bool &eFlag);

void setup() {
  Serial.begin(115200);
  pinMode(UWB_EN, OUTPUT);
  digitalWrite(UWB_EN, LOW);
  pinMode(DW1000_RST, OUTPUT);
  digitalWrite(DW1000_RST, LOW);
  delay(1000); // Esperamos un segundo para asegurarnos de que el puerto serie esté listo
  initBatteryPins();  
  hookBattery(true); // Habilitamos alimentación por batería si no hay USB conectado

  Serial.println(F("Iniciando dispositivo:"));
  pinMode(LED, OUTPUT);


  // Serial.println("Flash OK. ID: 0x" + String(flash.getJEDECID(), HEX));

  if (anchorMasterEnabled) {
    writeWiFiCredentials("INCENTRIC 2G", "Vitain2025@"); // Reescribe para asegurar formato correcto en flash
  }

  xTaskCreatePinnedToCore(
                    DWM1000Loop,   /* Task function. */
                    "DWM1000Loop",     /* name of task. */
                    20000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    5,           /* priority of the task */
                    &DWM1000LoopTask,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  delay(100); 

  // create a task that will be executed in the Task2code() function, with priority 1 and executed on core 1
  xTaskCreatePinnedToCore(
                    Task2code,   /* Task function. */
                    "Task2",     /* name of task. */
                    20000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task2,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 1 */
  readContinuousMode(isContinuous); // Leemos el modo continuo desde flash al iniciar
  Serial.println(String("Modo continuo: ") + (isContinuous ? "ON" : "OFF"));
}

void DWM1000Loop( void * pvParameters ) {
  Serial.print("DWM1000Loop running on core ");
  Serial.println(xPortGetCoreID());
  setupDW1000();
  const int counterfinal =0;
  static byte counter = 0;
  static bool  eFlag= 0;
  static unsigned long eTimer = 0;
  for(;;){
    if (g_usbConnected) {
      usbConnectionLoop(eFlag);
    } else {
      eFlag = 0; // Reiniciamos el flag para permitir reintentos de ESP-NOW en el futuro
      loopDW1000();
      if (++counter % 20 == 0) { // Cada 20 iteraciones (aprox. cada 2 segundos)
        vTaskDelay(1 / portTICK_PERIOD_MS); 
      }
    }
  } 
}

void usbConnectionLoop(bool &eFlag){
  static unsigned long eTimer = 0;
  static byte counter = 0;
  if(anchorMasterEnabled) {
    if (eFlag == 0) 
    {
      getBluetoothData(); // Esperamos datos por Bluetooth antes de intentar ESP-NOW
      if (!g_usbConnected) {
        return;
      }
      eFlag = 1;
    }
    Serial.println("[Tag] Intentando inicializar ESP-NOW...");
    if(!initEspNow()) {
      Serial.println("[ESP-NOW] Error inicializando ESP-NOW.");
      return;
    }
    eTimer= millis();
    Serial.println("[ESP-NOW] ESP-NOW inicializado. Transmitiendo credenciales por ESP-NOW durante 2 minutos o hasta que se desconecte USB...");
    while(millis() - eTimer < 120000) { // Enviamos credenciales por ESP-NOW durante 2 minutos después de inicializarlo
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      if(!g_usbConnected){
        stopEspNow();
        eFlag = 0;
        return;
      }
      periodicBroadcastCredentials() ; // Difunde credenciales si es master y hay credenciales en flash
    }
    stopEspNow(); // Detenemos ESP-NOW para liberar WiFi y permitir reconexión por Bluetooth en el futuro
  }
  #if DW1000_USB_CONTINUOUS_MODE
    eFlag = 0; // Reiniciamos el flag para permitir reintentos de ESP-NOW en el futuro  
    loopDW1000();     
    if (++counter % 100 == 0) { // Cada 20 iteraciones (aprox. cada 2 segundos)
      vTaskDelay(1 / portTICK_PERIOD_MS); 
    } 
  #else
    Serial.println("[ESP-NOW] Esperando desconexión de USB para reactivar ESP-NOW...");
    stopEspNow();
    // eFlag = 0; // Reiniciamos el flag para permitir reintentos de ESP-NOW en el futuro
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  #endif
}
static void initSystemWatchdog() {
    // esp_task_wdt_init() acepta ser llamado cuando el TWDT ya está activo;
    // en ese caso solo actualiza el timeout y el flag de pánico.
    // El timeout es en segundos (API ESP-IDF v4.x).
    esp_task_wdt_init(UWB_CPU_WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL); // suscribe la tarea actual (Task1)
    esp_task_wdt_reset();   // alimentación inicial
}

//Task2code: blinks an LED every 700 ms
void Task2code( void * pvParameters ){
  Serial.print("Task2 running on core ");
  Serial.println(xPortGetCoreID());
  Serial.println("Iniciando flash WiFi...");
  if (!initFlash()) 
    while (1) {
      delay(1000);
      Serial.println("Error inicializando flash.");
    } 
  static unsigned long lastDiagnostic = 0;
  static bool ledState = false;
  uint16_t vMv;
  static uint16_t delayMs = 1000;
  static bool prev_usbConnected = (readBatteryVoltageMv() >= USB_VOLTAGE_THRESHOLD_MV);
  initSystemWatchdog();
  for(;;){
    vTaskDelay(200 / portTICK_PERIOD_MS);
    if (millis() - lastDiagnostic >= delayMs) {
      lastDiagnostic = millis();
      ledState = !ledState;
      digitalWrite(LED, ledState ? HIGH : LOW);
      vMv = readBatteryVoltageMv();
      g_usbConnected = (vMv >= USB_VOLTAGE_THRESHOLD_MV);
      if (g_usbConnected) {
        delayMs = 200; // Si el USB está conectado, hacemos diagnósticos cada 10 segundos
      } else {
        delayMs = 1600; // Si el USB no está conectado, hacemos diagnósticos cada segundo
      }
      esp_task_wdt_reset();
      // Serial.printf("USB voltage: %u mV (%s)\n", vMv, g_usbConnected ? "conectado" : "desconectado");
    }
  }
}


bool getBluetoothData(){
  unsigned long now = millis();
  initBluetooth();
  bool commandReceived = false;
  delay(2000); // Esperamos un momento para que el Bluetooth se estabilice y pueda recibir comandos
  Serial.println("[Bluetooth] Esperando comandos BLE por hasta 90 segundos o hasta que se desconecte USB...");
  while (millis() - now < 90000 && g_usbConnected) { // Esperamos hasta 90 segundos para recibir datos por Bluetooth
  // while (1) { // Esperamos hasta 60 segundos para recibir datos por Bluetooth
    commandReceived = processBLECommands(); // Procesamos comandos BLE encolados
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Pequeña pausa para evitar bloquear el loop
  }
  Serial.println("[Bluetooth] Fin de espera ");
  stopBluetooth();
  return commandReceived;
}
void loop() {
  // static byte counter = 0;z
  // loopDW1000();
  //   if (++counter % 20 == 0) { // Cada 20 iteraciones (aprox. cada 2 segundos)
  //     // Serial.println("Counter: " + String(counter++));
  //     vTaskDelay(1 / portTICK_PERIOD_MS); 
  //   }
}

