#include <Arduino.h>
#include "RF433Receiver.h"
#include <Preferences.h>
#include "GateControl.h"

namespace RF433Receiver {
  static RCSwitch* rcSwitch = nullptr;
  static Preferences prefs;
  
  // Массив для хранения доверенных ключей
  static const int MAX_KEYS = 10;
  static unsigned long trustedKeys[MAX_KEYS] = {0};
  static int keyCount = 0;

  void init(RCSwitch* _rcSwitch) {
    rcSwitch = _rcSwitch;
    prefs.begin("rf433", false);
    loadKeys();
    
    Serial.println("[RF433Receiver] 433MHz приемник инициализирован");
    Serial.printf("[RF433Receiver] Загружено ключей: %d\n", keyCount);
  }

  void handleReceivedCode(unsigned long receivedCode, unsigned int bitLength, unsigned int protocol) {
    if (receivedCode == 0) {
      return; // Игнорируем нулевые коды
    }
    
    Serial.println("[RF433Receiver] =========================");
    Serial.print("[RF433Receiver] Получен код: ");
    Serial.println(receivedCode);
    Serial.print("[RF433Receiver] Длина бит: ");
    Serial.println(bitLength);
    Serial.print("[RF433Receiver] Протокол: ");
    Serial.println(protocol);
    Serial.println("[RF433Receiver] =========================");
    
    // Проверка, является ли код доверенным
    if (isTrustedKey(receivedCode)) {
      Serial.println("[RF433Receiver] ✓ Код доверенный! Активация ворот...");
      GateControl::triggerGatePulse();
    } else {
      Serial.println("[RF433Receiver] ✗ Код не найден в списке доверенных");
      Serial.println("[RF433Receiver] Для добавления используйте addKey()");
    }
  }

  bool isTrustedKey(unsigned long code) {
    for (int i = 0; i < keyCount; i++) {
      if (trustedKeys[i] == code) {
        return true;
      }
    }
    return false;
  }

  void addKey(unsigned long code) {
    // Проверка, не добавлен ли уже этот ключ
    if (isTrustedKey(code)) {
      Serial.println("[RF433Receiver] Ключ уже существует");
      return;
    }
    
    // Проверка, не превышено ли максимальное количество ключей
    if (keyCount >= MAX_KEYS) {
      Serial.println("[RF433Receiver] Достигнуто максимальное количество ключей");
      return;
    }
    
    trustedKeys[keyCount] = code;
    keyCount++;
    saveKeys();
    
    Serial.printf("[RF433Receiver] Ключ добавлен: %lu (всего: %d)\n", code, keyCount);
  }

  void removeKey(unsigned long code) {
    int indexToRemove = -1;
    
    // Поиск ключа
    for (int i = 0; i < keyCount; i++) {
      if (trustedKeys[i] == code) {
        indexToRemove = i;
        break;
      }
    }
    
    if (indexToRemove == -1) {
      Serial.println("[RF433Receiver] Ключ не найден");
      return;
    }
    
    // Сдвиг массива
    for (int i = indexToRemove; i < keyCount - 1; i++) {
      trustedKeys[i] = trustedKeys[i + 1];
    }
    
    keyCount--;
    trustedKeys[keyCount] = 0; // Очистка последнего элемента
    saveKeys();
    
    Serial.printf("[RF433Receiver] Ключ удален: %lu (осталось: %d)\n", code, keyCount);
  }

  void loadKeys() {
    keyCount = prefs.getInt("keyCount", 0);
    
    if (keyCount > MAX_KEYS) {
      keyCount = 0; // Защита от некорректных данных
    }
    
    for (int i = 0; i < keyCount; i++) {
      String keyName = "key" + String(i);
      trustedKeys[i] = prefs.getULong(keyName.c_str(), 0);
      Serial.printf("[RF433Receiver] Загружен ключ %d: %lu\n", i, trustedKeys[i]);
    }
    
    Serial.printf("[RF433Receiver] Загружено ключей из NVS: %d\n", keyCount);
  }

  void saveKeys() {
    prefs.putInt("keyCount", keyCount);
    
    for (int i = 0; i < keyCount; i++) {
      String keyName = "key" + String(i);
      prefs.putULong(keyName.c_str(), trustedKeys[i]);
    }
    
    Serial.printf("[RF433Receiver] Ключи сохранены в NVS: %d\n", keyCount);
  }
}

