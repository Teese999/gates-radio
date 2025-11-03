#include <Arduino.h>
#include "GSMManager.h"
#include "infrastructure/Logger.h"
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
      Logger::logf("info", "[GSMManager] Загружен номер %d: %s", i, trustedNumbers[i].c_str());
    }
    
    Logger::info("GSM Manager инициализирован (заглушка)");
    Logger::warning("Для полной работы подключите модуль SIM800L");
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
      Logger::warning("GSM номер уже существует: " + number);
      return;
    }
    
    // Проверка, не превышено ли максимальное количество номеров
    if (numberCount >= MAX_NUMBERS) {
      Logger::warning("Достигнуто максимальное количество GSM номеров");
      return;
    }
    
    trustedNumbers[numberCount] = number;
    numberCount++;
    
    // Сохранение в NVS
    prefs.putInt("numCount", numberCount);
    String keyName = "num" + String(numberCount - 1);
    prefs.putString(keyName.c_str(), number);
    
    Logger::success("GSM номер добавлен: " + number + " (всего: " + String(numberCount) + ")");
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
      Logger::warning("GSM номер не найден: " + number);
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
    
    Logger::warning("GSM номер удален: " + number + " (осталось: " + String(numberCount) + ")");
  }
}

