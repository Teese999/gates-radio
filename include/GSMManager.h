#ifndef GSM_MANAGER_H
#define GSM_MANAGER_H

#include <Arduino.h>

/**
 * Модуль GSMManager.h
 * Управление GSM модулем: прием входящих звонков и SMS, обработка доверенных номеров.
 * Опциональный модуль - будет реализован при подключении SIM800L
 */
namespace GSMManager {
  /**
   * Инициализация GSM модуля
   * @param txPin - TX пин UART для GSM модуля
   * @param rxPin - RX пин UART для GSM модуля
   */
  void init(int txPin, int rxPin);
  
  /**
   * Обработка GSM событий
   * Должна вызываться в цикле loop()
   */
  void handleGSM();
  
  /**
   * Проверка, является ли номер доверенным
   * @param number - номер телефона для проверки
   * @return true если номер доверенный, false если нет
   */
  bool isTrustedNumber(const String& number);
  
  /**
   * Добавление доверенного номера
   * @param number - номер телефона для добавления
   */
  void addTrustedNumber(const String& number);
  
  /**
   * Удаление доверенного номера
   * @param number - номер телефона для удаления
   */
  void removeTrustedNumber(const String& number);
}

#endif // GSM_MANAGER_H


