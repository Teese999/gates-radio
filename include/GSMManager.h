#ifndef GSM_MANAGER_H
#define GSM_MANAGER_H

#include <Arduino.h>
#include <functional>

/**
 * Модуль GSMManager.h
 * Управление GSM модулем SIM800L (UART2): приём входящих звонков и SMS,
 * открытие ворот по номерам из белого списка.
 *
 * Белый список хранится в main.cpp (systemState.phones, управляется через веб-UI),
 * поэтому модуль не имеет своего хранилища — main.cpp передаёт колбэки.
 *
 * Используются только NET (антенна), VCC, GND, RXD, TXD модуля.
 * Микрофон/динамик/DTR/RING не задействованы.
 */
namespace GSMManager {
  /**
   * Колбэк проверки номера по белому списку.
   * @param number - номер в том виде, как его прислал модуль (+CLIP/+CMT)
   * @param isCall - true = входящий звонок (флаг callEnabled), false = SMS (smsEnabled)
   * @return true, если номер доверенный для этого канала
   */
  typedef std::function<bool(const String& number, bool isCall)> TrustedCheckFn;

  /**
   * Колбэк открытия ворот.
   * @param source - описание источника для логов ("Звонок +7...", "SMS +7...")
   */
  typedef std::function<void(const String& source)> GateOpenFn;

  /**
   * Инициализация GSM модуля (неблокирующая — реальная настройка SIM800L
   * идёт state machine'ой внутри handleGSM, модуль может подключиться позже)
   * @param rxPin - RX пин ESP32 (подключается к TXD модуля)
   * @param txPin - TX пин ESP32 (подключается к RXD модуля)
   * @param trustedCheck - проверка номера по белому списку
   * @param gateOpen - открытие ворот
   */
  void init(int rxPin, int txPin, TrustedCheckFn trustedCheck, GateOpenFn gateOpen);

  /**
   * Обработка GSM событий (инициализация модуля, +CLIP, +CMT).
   * Вызывать на каждой итерации loop(). Не блокирует.
   */
  void handleGSM();

  /**
   * @return true, если SIM800L ответил и сконфигурирован (АОН + SMS в UART)
   */
  bool isReady();
}

#endif // GSM_MANAGER_H
