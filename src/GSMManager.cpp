#include <Arduino.h>
#include "GSMManager.h"
#include <Preferences.h>

namespace GSMManager {
  static Preferences prefs;
  static HardwareSerial* gsmSerial = nullptr;
  
  // Массив для хранения доверенных номеров
  static const int MAX_NUMBERS = 5;
  static String trustedNumbers[MAX_NUMBERS];
  static int numberCount = 0;

  void init(int txPin, int rxPin) {
    // TODO: Инициализация GSM модуля через HardwareSerial
    // gsmSerial = new HardwareSerial(2);
    // gsmSerial->begin(9600, SERIAL_8N1, rxPin, txPin);
    
    prefs.begin("gsm", false);
    
    // Загрузка доверенных номеров
    numberCount = prefs.getInt("numCount", 0);
    if (numberCount > MAX_NUMBERS) {
      numberCount = 0;
    }
    
    for (int i = 0; i < numberCount; i++) {
      String keyName = "num" + String(i);
      trustedNumbers[i] = prefs.getString(keyName.c_str(), "");
      Serial.printf("[GSMManager] Загружен номер %d: %s\n", i, trustedNumbers[i].c_str());
    }
    
    Serial.println("[GSMManager] GSM Manager инициализирован (заглушка)");
    Serial.println("[GSMManager] ⚠️ Для полной работы подключите модуль SIM800L");
  }

  void handleGSM() {
    // TODO: Обработка входящих звонков и SMS
    // Проверка наличия данных от GSM модуля
    // При получении звонка/SMS от доверенного номера - активировать ворота
    
    // Заглушка
    // if (gsmSerial && gsmSerial->available()) {
    //   String response = gsmSerial->readString();
    //   Serial.print("[GSMManager] GSM: ");
    //   Serial.println(response);
    // }
  }

  bool isTrustedNumber(const String& number) {
    for (int i = 0; i < numberCount; i++) {
      if (trustedNumbers[i] == number) {
        return true;
      }
    }
    return false;
  }

  void addTrustedNumber(const String& number) {
    // Проверка, не добавлен ли уже этот номер
    if (isTrustedNumber(number)) {
      Serial.println("[GSMManager] Номер уже существует");
      return;
    }
    
    // Проверка, не превышено ли максимальное количество номеров
    if (numberCount >= MAX_NUMBERS) {
      Serial.println("[GSMManager] Достигнуто максимальное количество номеров");
      return;
    }
    
    trustedNumbers[numberCount] = number;
    numberCount++;
    
    // Сохранение в NVS
    prefs.putInt("numCount", numberCount);
    String keyName = "num" + String(numberCount - 1);
    prefs.putString(keyName.c_str(), number);
    
    Serial.printf("[GSMManager] Номер добавлен: %s (всего: %d)\n", number.c_str(), numberCount);
  }

  void removeTrustedNumber(const String& number) {
    int indexToRemove = -1;
    
    // Поиск номера
    for (int i = 0; i < numberCount; i++) {
      if (trustedNumbers[i] == number) {
        indexToRemove = i;
        break;
      }
    }
    
    if (indexToRemove == -1) {
      Serial.println("[GSMManager] Номер не найден");
      return;
    }
    
    // Сдвиг массива
    for (int i = indexToRemove; i < numberCount - 1; i++) {
      trustedNumbers[i] = trustedNumbers[i + 1];
    }
    
    numberCount--;
    trustedNumbers[numberCount] = ""; // Очистка последнего элемента
    
    // Сохранение в NVS
    prefs.putInt("numCount", numberCount);
    for (int i = 0; i < numberCount; i++) {
      String keyName = "num" + String(i);
      prefs.putString(keyName.c_str(), trustedNumbers[i]);
    }
    
    Serial.printf("[GSMManager] Номер удален: %s (осталось: %d)\n", number.c_str(), numberCount);
  }
}

