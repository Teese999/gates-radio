#ifndef RF433_RECEIVER_H
#define RF433_RECEIVER_H

#include <Arduino.h>
#include <RCSwitch.h>

/**
 * Модуль RF433Receiver.h
 * Прием и декодирование 433MHz сигналов, хранение и проверка доверенных ключей.
 * Использует Preferences для сохранения ключей.
 */
namespace RF433Receiver {
  /**
   * Инициализация приемника 433MHz
   * @param rcSwitch - указатель на объект RCSwitch
   */
  void init(RCSwitch* rcSwitch);
  
  /**
   * Обработка полученного кода с брелка
   * @param receivedCode - полученный код
   * @param bitLength - длина кода в битах
   * @param protocol - используемый протокол
   */
  void handleReceivedCode(unsigned long receivedCode, unsigned int bitLength, unsigned int protocol);
  
  /**
   * Проверка, является ли код доверенным
   * @param code - код для проверки
   * @return true если код доверенный, false если нет
   */
  bool isTrustedKey(unsigned long code);
  
  /**
   * Добавление нового доверенного ключа
   * @param code - код для добавления
   */
  void addKey(unsigned long code);
  
  /**
   * Удаление доверенного ключа
   * @param code - код для удаления
   */
  void removeKey(unsigned long code);
  
  /**
   * Загрузка сохраненных ключей из NVS
   */
  void loadKeys();
  
  /**
   * Сохранение ключей в NVS
   */
  void saveKeys();
}

#endif // RF433_RECEIVER_H


