#include <Arduino.h>
#include "GateControl.h"

namespace GateControl {
  static int ledPin = 0;

  void init(int _ledPin) {
    ledPin = _ledPin;
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW); // Изначально выключен
    
    Serial.println("[GateControl] Gate Control инициализирован");
    Serial.printf("[GateControl] LED Pin: %d\n", ledPin);
  }

  void triggerGatePulse(int duration_ms) {
    Serial.println("[GateControl] ===========================");
    Serial.println("[GateControl] 🚪 АКТИВАЦИЯ ВОРОТ!");
    Serial.printf("[GateControl] Длительность импульса: %d мс\n", duration_ms);
    Serial.println("[GateControl] ===========================");
    
    // Включаем светодиод/реле
    digitalWrite(ledPin, HIGH);
    
    // Ждем заданное время
    delay(duration_ms);
    
    // Выключаем светодиод/реле
    digitalWrite(ledPin, LOW);
    
    Serial.println("[GateControl] Импульс завершен");
  }
}

