#include <Arduino.h>
#include "InputManager.h"

namespace InputManager {
  static int vrxPin = 0;
  static int vryPin = 0;
  static int swPin = 0;
  
  static int currentMenuState = 0;
  static const int MAX_MENU_ITEMS = 5;
  
  static unsigned long lastDebounceTime = 0;
  static const unsigned long DEBOUNCE_DELAY = 200;
  static const int JOYSTICK_THRESHOLD = 1000; // Порог для определения движения джойстика

  void init(int _vrxPin, int _vryPin, int _swPin) {
    vrxPin = _vrxPin;
    vryPin = _vryPin;
    swPin = _swPin;
    
    // Настройка пинов
    pinMode(vrxPin, INPUT);
    pinMode(vryPin, INPUT);
    pinMode(swPin, INPUT_PULLUP); // Кнопка с подтяжкой
    
    Serial.println("[InputManager] Джойстик инициализирован");
    Serial.printf("[InputManager] VRx: %d, VRy: %d, SW: %d\n", vrxPin, vryPin, swPin);
  }

  void handleInput() {
    unsigned long currentTime = millis();
    
    // Защита от дребезга
    if (currentTime - lastDebounceTime < DEBOUNCE_DELAY) {
      return;
    }
    
    // Чтение значений джойстика
    int vrxValue = analogRead(vrxPin);
    int vryValue = analogRead(vryPin);
    
    // Навигация вверх (джойстик вниз по оси Y)
    if (vryValue > 3000) {
      currentMenuState--;
      if (currentMenuState < 0) {
        currentMenuState = MAX_MENU_ITEMS - 1;
      }
      lastDebounceTime = currentTime;
      Serial.printf("[InputManager] Меню вверх: %d\n", currentMenuState);
    }
    
    // Навигация вниз (джойстик вверх по оси Y)
    else if (vryValue < 1000) {
      currentMenuState++;
      if (currentMenuState >= MAX_MENU_ITEMS) {
        currentMenuState = 0;
      }
      lastDebounceTime = currentTime;
      Serial.printf("[InputManager] Меню вниз: %d\n", currentMenuState);
    }
    
    // Проверка нажатия кнопки
    if (digitalRead(swPin) == LOW) {
      lastDebounceTime = currentTime;
      Serial.printf("[InputManager] Кнопка нажата! Выбран пункт: %d\n", currentMenuState);
      
      // Здесь можно добавить обработку выбора пункта меню
      switch (currentMenuState) {
        case 0:
          Serial.println("[InputManager] Действие: Открыть ворота");
          break;
        case 1:
          Serial.println("[InputManager] Действие: Настройки WiFi");
          break;
        case 2:
          Serial.println("[InputManager] Действие: Управление ключами");
          break;
        case 3:
          Serial.println("[InputManager] Действие: Статус системы");
          break;
        case 4:
          Serial.println("[InputManager] Действие: GSM настройки");
          break;
      }
    }
  }

  int getMenuState() {
    return currentMenuState;
  }

  bool isButtonPressed() {
    return (digitalRead(swPin) == LOW);
  }
}

