#include <Arduino.h>
#include "GateControl.h"
#include "infrastructure/Logger.h"

namespace GateControl {
  static int openPin = -1;
  static int closePin = -1;

  // Фазы цикла: открытие (канал 1) → открыто (оба выкл) → закрытие (канал 2).
  // Пошаговая логика команд: движение → STOPPED → движение в обратную сторону.
  // REVERSE_PAUSE — защитная пауза перед сменой направления:
  // мгновенный реверс мотора без мёртвого времени даёт бросок тока.
  enum class Phase { IDLE, OPENING, WAITING, CLOSING, STOPPED, REVERSE_PAUSE };
  static Phase phase = Phase::IDLE;
  static Phase nextDir = Phase::OPENING; // Куда ехать после STOPPED/REVERSE_PAUSE

  static unsigned long phaseStart = 0;     // Начало текущей фазы (millis)
  static unsigned long openDuration = 0;   // Время полного хода на открытие, мс
  static unsigned long stayDuration = 0;   // Пауза «открыто», мс
  static unsigned long closeDuration = 0;  // Время полного хода на закрытие, мс

  // Позиция створки по времени хода: 0.0 = закрыто, 1.0 = открыто.
  // Фаза движения длится остаток пути, а не полный тайминг: приоткрыли на 60% —
  // закрытие займёт 60% времени закрытия (и симметрично для недозакрытых).
  static float position = 0.0f;
  static float phaseStartPos = 0.0f;       // Позиция на входе в фазу движения
  static unsigned long phaseDuration = 0;  // Длительность текущей фазы движения, мс

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
        phaseStartPos = position;
        phaseDuration = (unsigned long)((1.0f - position) * (float)openDuration);
        setChannels(true, false);
        Logger::success("[Ворота] Открытие (" + String(phaseDuration / 1000.0f, 1) + " с)");
        break;
      case Phase::WAITING:
        position = 1.0f;
        setChannels(false, false);
        Logger::info("[Ворота] Открыто, автозакрытие через " + String(stayDuration / 1000) + " с");
        break;
      case Phase::CLOSING:
        phaseStartPos = position;
        phaseDuration = (unsigned long)(position * (float)closeDuration);
        setChannels(false, true);
        Logger::info("[Ворота] Закрытие (" + String(phaseDuration / 1000.0f, 1) + " с)");
        break;
      case Phase::STOPPED:
        setChannels(false, false);
        Logger::warning("[Ворота] Остановлено, следующая команда — " +
                        String(nextDir == Phase::CLOSING ? "закрытие" : "открытие"));
        break;
      case Phase::REVERSE_PAUSE:
        setChannels(false, false);
        break;
      case Phase::IDLE:
        position = 0.0f;
        setChannels(false, false);
        Logger::info("[Ворота] Цикл завершен");
        break;
    }
  }

  // Зафиксировать позицию створки в момент остановки движения
  static void freezePosition() {
    unsigned long elapsed = millis() - phaseStart;
    if (phase == Phase::OPENING && openDuration > 0) {
      position = phaseStartPos + (float)elapsed / (float)openDuration;
      if (position > 1.0f) position = 1.0f;
    } else if (phase == Phase::CLOSING && closeDuration > 0) {
      position = phaseStartPos - (float)elapsed / (float)closeDuration;
      if (position < 0.0f) position = 0.0f;
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
        // Движение прервано командой — стоп, следующая команда закроет
        freezePosition();
        nextDir = Phase::CLOSING;
        enterPhase(Phase::STOPPED);
        break;
      case Phase::CLOSING:
        // Движение прервано командой — стоп, следующая команда откроет
        freezePosition();
        nextDir = Phase::OPENING;
        enterPhase(Phase::STOPPED);
        break;
      case Phase::STOPPED:
        // Едем в обратную сторону через защитную паузу
        enterPhase(Phase::REVERSE_PAUSE);
        break;
      case Phase::WAITING:
        // Ворота открыты — команда закрывает, не дожидаясь автозакрытия
        enterPhase(Phase::CLOSING);
        break;
      case Phase::REVERSE_PAUSE:
        // Переходные 500 мс — глотаем дребезг повторных нажатий
        Logger::info("[Ворота] Переходная пауза, сигнал пропущен");
        break;
    }
  }

  void update() {
    if (phase == Phase::IDLE) return;

    // Сравнение через беззнаковое вычитание корректно переживает переполнение millis()
    unsigned long elapsed = millis() - phaseStart;

    switch (phase) {
      case Phase::OPENING:
        if (elapsed >= phaseDuration) enterPhase(Phase::WAITING);
        break;
      case Phase::WAITING:
        if (elapsed >= stayDuration) enterPhase(Phase::CLOSING);
        break;
      case Phase::CLOSING:
        if (elapsed >= phaseDuration) enterPhase(Phase::IDLE);
        break;
      case Phase::REVERSE_PAUSE:
        if (elapsed >= REVERSE_PAUSE_MS) enterPhase(nextDir);
        break;
      case Phase::STOPPED: // Стоим, пока не придёт следующая команда
      case Phase::IDLE:
        break;
    }
  }

  bool isCycleActive() {
    return phase != Phase::IDLE;
  }

  const char* phaseName() {
    switch (phase) {
      case Phase::OPENING: return "opening";
      case Phase::WAITING: return "open";
      case Phase::CLOSING: return "closing";
      case Phase::STOPPED: return "stopped";
      case Phase::REVERSE_PAUSE:
        return nextDir == Phase::CLOSING ? "closing" : "opening";
      case Phase::IDLE: break;
    }
    return "closed";
  }

  const char* nextDirName() {
    return nextDir == Phase::CLOSING ? "close" : "open";
  }

  float positionNow() {
    if (phase == Phase::OPENING && openDuration > 0) {
      float p = phaseStartPos + (float)(millis() - phaseStart) / (float)openDuration;
      return p > 1.0f ? 1.0f : p;
    }
    if (phase == Phase::CLOSING && closeDuration > 0) {
      float p = phaseStartPos - (float)(millis() - phaseStart) / (float)closeDuration;
      return p < 0.0f ? 0.0f : p;
    }
    return position;
  }
}
