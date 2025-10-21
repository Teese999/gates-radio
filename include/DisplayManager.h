#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Adafruit_ST7789.h>

/**
 * Модуль DisplayManager.h
 * Управление дисплеем: инициализация, отрисовка меню, текста, сплэш-скрина.
 */
namespace DisplayManager {
  /**
   * Инициализация дисплея
   * @param display - указатель на объект дисплея Adafruit_ST7789
   */
  void init(Adafruit_ST7789* display);
  
  /**
   * Показать начальный экран загрузки
   */
  void showSplashScreen();
  
  /**
   * Обновление отображения меню
   * @param menuState - текущее состояние меню (номер пункта)
   */
  void updateMenu(int menuState);
  
  /**
   * Показать экран настройки Wi-Fi
   */
  void showWifiConfigScreen();
  
  /**
   * Показать экран управления ключами 433MHz
   */
  void showKeyManagementScreen();
  
  /**
   * Вывести сообщение на дисплей
   * @param message - текст сообщения
   * @param duration_ms - время отображения в миллисекундах (0 = постоянно)
   */
  void printMessage(const String& message, int duration_ms = 0);
}

#endif // DISPLAY_MANAGER_H

