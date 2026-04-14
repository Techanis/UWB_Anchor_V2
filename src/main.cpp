#include "config.h"
#include "memory.h"
#include "battery.h"
#include "dw1000_app.h"

// Declaración de tareas
TaskHandle_t Task1;
TaskHandle_t Task2;

// Global variables
String ssid;
String password;

// put function declarations here:
int myFunction(int, int);
bool connectWiFi();
void Task1code( void * pvParameters );
void Task2code( void * pvParameters );

void setup() {
  SERIAL_LOG.begin(115200);
  delay(5000); // Esperamos un segundo para asegurarnos de que el puerto serie esté listo
  initBatteryPins();  
  hookBattery(true); // Habilitamos alimentación por batería si no hay USB conectado
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

  Serial.println("Flash OK. ID: 0x" + String(flash.getJEDECID(), HEX));
  
  // Ejemplo: escribir credenciales (solo una vez)
  // if (writeWiFiCredentials("MARGOT-2.4G", "7054201030J")) {
  //   SERIAL_LOG.println("Credenciales guardadas.");
  // }
  
  // // Conectar
  // if (connectWiFi()) {
  //   Serial.print("Conectado. IP: ");
  //   Serial.println(WiFi.localIP());
  // } else {
  //   SERIAL_LOG.println("Fallo en conexión WiFi.");
  //   SERIAL_LOG.println("Borrando credenciales para reintentar...");
  //   eraseWiFiCredentials();
  // }
    //create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
                    Task1code,   /* Task function. */
                    "Task1",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task1,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  delay(500); 

  //create a task that will be executed in the Task2code() function, with priority 1 and executed on core 1
  xTaskCreatePinnedToCore(
                    Task2code,   /* Task function. */
                    "Task2",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task2,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 1 */
    delay(500); 

}

//Task1code: blinks an LED every 1000 ms
void Task1code( void * pvParameters ){
  Serial.print("Task1 running on core ");
  Serial.println(xPortGetCoreID());

  for(;;){
    loopDW1000();
    vTaskDelay(1 / portTICK_PERIOD_MS); // 1000ms
  } 
}

//Task2code: blinks an LED every 700 ms
void Task2code( void * pvParameters ){
  Serial.print("Task2 running on core ");
  Serial.println(xPortGetCoreID());
  static unsigned long lastDiagnostic = 0;
  static bool ledState = false;
  for(;;){

  if (millis() - lastDiagnostic >= 2000) {
    lastDiagnostic = millis();
    ledState = !ledState;
    digitalWrite(LED, ledState ? HIGH : LOW);
   }
  }
}

void loop() {
  //   if (digitalRead(DW1000_EXTON) == LOW) {
  //     SERIAL_LOG.println("DW1000 en modo de bajo consumo EXTON");
  //   } else {
  //     SERIAL_LOG.println("DW1000 activo EXTON");
  //   }
  //   if (digitalRead(DW1000_RST) == LOW) {
  //     SERIAL_LOG.println("DW1000 en modo de bajo consumo RST");
  //   } else {
  //     SERIAL_LOG.println("DW1000 activo RST");
  //   }
  //   SERIAL_LOG.print("Medición de batería: ");
  //   SERIAL_LOG.println(readBatteryADC());
  //   SERIAL_LOG.println(isUSBConnected() ? "Fuente USB conectada" : "Fuente USB no conectada");
   
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
    vTaskDelay(500 / portTICK_PERIOD_MS); // 1000ms
  }
  return WiFi.status() == WL_CONNECTED;
}