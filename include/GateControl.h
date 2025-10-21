#ifndef GATE_CONTROL_H
#define GATE_CONTROL_H

#include <Arduino.h>

/**
 * Модуль GateControl.h
 * Управление подачей импульса на диод/ворота.
 */
namespace GateControl {
  /**
   * Инициализация управления воротами
   * @param ledPin - пин для управления светодиодом/реле
   */
  void init(int ledPin);
  
  /**
   * Подача импульса на ворота
   * @param duration_ms - длительность импульса в миллисекундах (по умолчанию 500мс)
   */
  void triggerGatePulse(int duration_ms = 500);
}

#endif // GATE_CONTROL_H


