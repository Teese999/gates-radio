#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>

/**
 * Модуль InputManager.h
 * Управление джойстиком: чтение аналоговых значений, обработка нажатий кнопки, навигация по меню.
 */
namespace InputManager {
  /**
   * Инициализация джойстика
   * @param vrxPin - пин для оси X
   * @param vryPin - пин для оси Y
   * @param swPin - пин для кнопки
   */
  void init(int vrxPin, int vryPin, int swPin);
  
  /**
   * Обработка ввода с джойстика
   * Должна вызываться в цикле loop()
   */
  void handleInput();
  
  /**
   * Получить текущее состояние меню
   * @return номер выбранного пункта меню
   */
  int getMenuState();
  
  /**
   * Проверить, нажата ли кнопка джойстика
   * @return true если кнопка нажата, false если нет
   */
  bool isButtonPressed();
}

#endif // INPUT_MANAGER_H


