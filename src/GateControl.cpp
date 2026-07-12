#include <Arduino.h>
#include "GateControl.h"
#include "infrastructure/Logger.h"

namespace GateControl {
  static int openPin = -1;
  static int closePin = -1;

  // Фазы цикла: открытие (канал 1) → открыто (оба выкл) → закрытие (канал 2).
  // REVERSE_PAUSE — защитная пауза при перезапуске цикла из фазы закрытия:
  // мгновенный реверс мотора без мёртвого времени даёт бросок тока.
  enum class Phase { IDLE, OPENING, WAITING, CLOSING, REVERSE_PAUSE };
  static Phase phase = Phase::IDLE;

  static unsigned long phaseStart = 0;     // Начало текущей фазы (millis)
  static unsigned long openDuration = 0;   // Длительность фазы открытия, мс
  static unsigned long stayDuration = 0;   // Пауза «открыто», мс
  static unsigned long closeDuration = 0;  // Длительность фазы закрытия, мс

  static const unsigned long REVERSE_PAUSE_MS = 500;

  // Interlock: любой активный канал включается только при выключенном втором
  static void setChannels(bool open, bool close) {
    if (open && close) return; // одновременно — никогда
    digitalWrite(openPin, open ? HIGH : LOW);
    digitalWrite(closePin, close ? HIGH : LOW);
  }

  static void enterPhase(Phase next) {
    phase = next;
    phaseStart = millis();
    switch (next) {
      case Phase::OPENING:
        setChannels(true, false);
        Logger::success("[Ворота] Открытие (" + String(openDuration / 1000.0f, 1) + " с)");
        break;
      case Phase::WAITING:
        setChannels(false, false);
        Logger::info("[Ворота] Открыто, автозакрытие через " + String(stayDuration / 1000) + " с");
        break;
      case Phase::CLOSING:
        setChannels(false, true);
        Logger::info("[Ворота] Закрытие (" + String(closeDuration / 1000.0f, 1) + " с)");
        break;
      case Phase::REVERSE_PAUSE:
        setChannels(false, false);
        break;
      case Phase::IDLE:
        setChannels(false, false);
        Logger::info("[Ворота] Цикл завершен");
        break;
    }
  }

  void init(int _openPin, int _closePin) {
    openPin = _openPin;
    closePin = _closePin;
    pinMode(openPin, OUTPUT);
    pinMode(closePin, OUTPUT);
    setChannels(false, false);
    phase = Phase::IDLE;

    Logger::success("Gate Control инициализирован (2 реле: открыть/закрыть)");
    Logger::logf("info", "[GateControl] Открыть: GPIO%d, закрыть: GPIO%d", openPin, closePin);
  }

  void startCycle(unsigned long openMs, unsigned long stayMs, unsigned long closeMs) {
    openDuration = openMs;
    stayDuration = stayMs;
    closeDuration = closeMs;

    switch (phase) {
      case Phase::IDLE:
        enterPhase(Phase::OPENING);
        break;
      case Phase::OPENING:
      case Phase::REVERSE_PAUSE:
        // Уже открываемся — повторный сигнал ничего не меняет
        Logger::info("[Ворота] Уже открываются, сигнал пропущен");
        break;
      case Phase::WAITING:
        // Продлеваем «открыто» заново
        phaseStart = millis();
        Logger::info("[Ворота] Пауза «открыто» продлена");
        break;
      case Phase::CLOSING:
        // Останавливаем закрытие, после мёртвой паузы откроем заново
        enterPhase(Phase::REVERSE_PAUSE);
        Logger::warning("[Ворота] Закрытие прервано, повторное открытие");
        break;
    }
  }

  void update() {
    if (phase == Phase::IDLE) return;

    // Сравнение через беззнаковое вычитание корректно переживает переполнение millis()
    unsigned long elapsed = millis() - phaseStart;

    switch (phase) {
      case Phase::OPENING:
        if (elapsed >= openDuration) enterPhase(Phase::WAITING);
        break;
      case Phase::WAITING:
        if (elapsed >= stayDuration) enterPhase(Phase::CLOSING);
        break;
      case Phase::CLOSING:
        if (elapsed >= closeDuration) enterPhase(Phase::IDLE);
        break;
      case Phase::REVERSE_PAUSE:
        if (elapsed >= REVERSE_PAUSE_MS) enterPhase(Phase::OPENING);
        break;
      case Phase::IDLE:
        break;
    }
  }

  bool isCycleActive() {
    return phase != Phase::IDLE;
  }
}
