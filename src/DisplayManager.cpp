#include <Arduino.h>
#include "DisplayManager.h"
#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>

namespace DisplayManager {
  static Adafruit_ST7789* display = nullptr;

  void init(Adafruit_ST7789* tft) {
    display = tft;
    
    // Инициализация дисплея ST7789 (240x240)
    display->init(240, 240);
    display->setRotation(0); // Ориентация экрана
    display->fillScreen(ST77XX_BLACK);
    
    Serial.println("[DisplayManager] Дисплей инициализирован");
  }

  void showSplashScreen() {
    if (!display) return;
    
    display->fillScreen(ST77XX_BLACK);
    display->setTextColor(ST77XX_WHITE);
    display->setTextSize(2);
    
    // Заголовок
    display->setCursor(20, 60);
    display->println("УМНЫЕ ВОРОТА");
    
    display->setTextSize(1);
    display->setCursor(40, 100);
    display->println("ESP32 DevKit");
    
    display->setCursor(30, 140);
    display->println("Инициализация...");
    
    Serial.println("[DisplayManager] Сплэш-скрин показан");
    delay(2000);
  }

  void updateMenu(int menuState) {
    if (!display) return;
    
    display->fillScreen(ST77XX_BLACK);
    display->setTextColor(ST77XX_WHITE);
    display->setTextSize(1);
    
    // Заголовок меню
    display->setCursor(10, 10);
    display->setTextColor(ST77XX_YELLOW);
    display->println("=== МЕНЮ ===");
    
    // Пункты меню
    display->setTextColor(ST77XX_WHITE);
    display->setCursor(10, 40);
    display->print(menuState == 0 ? "> " : "  ");
    display->println("1. Открыть ворота");
    
    display->setCursor(10, 60);
    display->print(menuState == 1 ? "> " : "  ");
    display->println("2. Настройки WiFi");
    
    display->setCursor(10, 80);
    display->print(menuState == 2 ? "> " : "  ");
    display->println("3. Управление ключами");
    
    display->setCursor(10, 100);
    display->print(menuState == 3 ? "> " : "  ");
    display->println("4. Статус системы");
    
    display->setCursor(10, 120);
    display->print(menuState == 4 ? "> " : "  ");
    display->println("5. GSM настройки");
  }

  void showWifiConfigScreen() {
    if (!display) return;
    
    display->fillScreen(ST77XX_BLACK);
    display->setTextColor(ST77XX_CYAN);
    display->setTextSize(1);
    
    display->setCursor(10, 10);
    display->println("=== WiFi НАСТРОЙКИ ===");
    
    display->setTextColor(ST77XX_WHITE);
    display->setCursor(10, 40);
    display->println("Подключитесь к");
    display->setCursor(10, 60);
    display->println("точке доступа:");
    
    display->setTextColor(ST77XX_YELLOW);
    display->setCursor(10, 90);
    display->println("SmartGate-Config");
    
    display->setTextColor(ST77XX_WHITE);
    display->setCursor(10, 120);
    display->println("IP: 192.168.4.1");
    
    Serial.println("[DisplayManager] WiFi Config экран показан");
  }

  void showKeyManagementScreen() {
    if (!display) return;
    
    display->fillScreen(ST77XX_BLACK);
    display->setTextColor(ST77XX_GREEN);
    display->setTextSize(1);
    
    display->setCursor(10, 10);
    display->println("=== КЛЮЧИ 433MHz ===");
    
    display->setTextColor(ST77XX_WHITE);
    display->setCursor(10, 40);
    display->println("Нажмите кнопку");
    display->setCursor(10, 60);
    display->println("на брелке для");
    display->setCursor(10, 80);
    display->println("добавления");
    
    Serial.println("[DisplayManager] Key Management экран показан");
  }

  void printMessage(const String& message, int duration_ms) {
    if (!display) return;
    
    display->fillScreen(ST77XX_BLACK);
    display->setTextColor(ST77XX_WHITE);
    display->setTextSize(1);
    display->setCursor(10, 100);
    display->println(message);
    
    Serial.print("[DisplayManager] Сообщение: ");
    Serial.println(message);
    
    if (duration_ms > 0) {
      delay(duration_ms);
    }
  }
}

