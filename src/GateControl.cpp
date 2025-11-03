#include <Arduino.h>
#include "GateControl.h"
#include "infrastructure/Logger.h"

namespace GateControl {
  static int ledPin = 0;

  void init(int _ledPin) {
    ledPin = _ledPin;
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW); // Изначально выключен
    
    Logger::success("Gate Control инициализирован");
    Logger::logf("info", "[GateControl] LED Pin: %d", ledPin);
  }

  void triggerGatePulse(int duration_ms) {
    Logger::success("АКТИВАЦИЯ ВОРОТ! Длительность импульса: " + String(duration_ms) + " мс");
    
    // Включаем светодиод/реле
    digitalWrite(ledPin, HIGH);
    
    // Ждем заданное время
    delay(duration_ms);
    
    // Выключаем светодиод/реле
    digitalWrite(ledPin, LOW);
    
    Logger::info("[GateControl] Импульс завершен");
  }
}

