#include "battery.h"
#include "config.h"

// Inicializa los pines de la gestión de alimentación/batería.
// - USB_CONNECTED: entrada para detectar si hay energía USB presente.
// - BAT_HOOK: salida para habilitar o cortar la alimentación por batería.
// - BAT_ADC_EN: salida para habilitar la medición de voltaje en el divisor de batería.
// - BAT_ADC: entrada ADC para leer el valor de voltaje de batería.

void initBatteryPins() {
  // pinMode(USB_CONNECTED, INPUT);
  pinMode(BAT_HOOK, OUTPUT);
  pinMode(BAT_ADC_EN, OUTPUT);
  pinMode(BAT_ADC, INPUT);

  //Programación de ADC para lectura de USB
  // analogReadResolution(9);
  // analogSetClockDiv(32); // ADC clock = 80MHz / 32 = 2.5 MHz (dentro del rango recomendado de 1-2.5 MHz para ESP32)
  // analogSetAttenuation(ADC_11db); // Rango de entrada 0-3.6V, suficiente para leer el divisor de batería incluso con USB presente

  // Estado por defecto
  digitalWrite(BAT_HOOK, LOW);   // inicialmente batería deshabilitada
  digitalWrite(BAT_ADC_EN, LOW); // no medir hasta que se solicite
}

// Retorna true si la fuente USB está conectada (energía presente)
// bool isUSBConnected() {
//   return digitalRead(USB_CONNECTED) == HIGH;
// }

// Lee el ADC de batería (valor crudo 0..4095 en ESP32) 
// (requiere habilitar primero BAT_ADC_EN en caso de circuito de habilitación)
int readBatteryADC() {
  digitalWrite(BAT_ADC_EN, HIGH);
  vTaskDelay(5 / portTICK_PERIOD_MS); // 5 ms
  int raw = analogRead(BAT_ADC);
  digitalWrite(BAT_ADC_EN, LOW);
  return raw;
}

void hookBattery(bool enable) {
  digitalWrite(BAT_HOOK, enable ? HIGH : LOW);
}

uint32_t readBatteryVoltageMv(){
  uint32_t suma = 0;

  for (int i = 0; i < NUM_MUESTRAS; i++) {
    suma += analogReadMilliVolts(USB_ADC);
    vTaskDelay(1 / portTICK_PERIOD_MS); // 1 ms entre muestras 
  }

  return suma / NUM_MUESTRAS;
}