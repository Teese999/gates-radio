#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <RCSwitch.h>

// Подключение кастомных модулей
#include "DisplayManager.h"
#include "InputManager.h"
#include "RF433Receiver.h"
#include "WiFiManager.h"
#include "GateControl.h"
#include "GSMManager.h"

// --- Константы пинов ---
// Пины для дисплея GMT130-V1.0 (ST7789)
#define TFT_CS    5   // GPIO5 - Chip Select pin
#define TFT_DC    2   // GPIO2 - Data/Command pin
#define TFT_RST   4   // GPIO4 - Reset pin
#define TFT_MOSI  23  // GPIO23 - SPI MOSI
#define TFT_SCLK  18  // GPIO18 - SPI Clock

// Пины для джойстика KS0008
#define JOY_VRX_PIN 34 // GPIO34 - Ось X (ADC1_CH6)
#define JOY_VRY_PIN 35 // GPIO35 - Ось Y (ADC1_CH7)
#define JOY_SW_PIN  14 // GPIO14 - Кнопка

// Пин для радиомодуля SRX882 v1.3
#define RF_DATA_PIN 13 // GPIO13 - Data pin

// Пин для светодиода
#define LED_PIN     12 // GPIO12 - Управление светодиодом

// Пины для GSM SIM800L (UART2)
#define GSM_RX_PIN  17 // GPIO17 - RX пин для UART2 (подключается к TX GSM модуля)
#define GSM_TX_PIN  16 // GPIO16 - TX пин для UART2 (подключается к RX GSM модуля)

// Глобальные объекты
Preferences preferences; // Для сохранения данных в NVS
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST); // Объект дисплея
RCSwitch mySwitch = RCSwitch(); // Объект 433MHz приемника
WebServer server(80); // Веб-сервер для конфигурации

// --- Setup Function ---
void setup() {
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("Проект: Умные Ворота");
  Serial.println("ESP32 DevKit 38 pin");
  Serial.println("=================================");
  Serial.println("Запуск системы...");

  // Инициализация NVS для сохранения настроек
  preferences.begin("smart-gate", false);
  Serial.println("[OK] Preferences инициализированы");

  // Инициализация DisplayManager
  DisplayManager::init(&tft);
  
  // Показать приветствие
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(60, 110);
  tft.println("hello");
  
  Serial.println("[OK] Дисплей инициализирован");

  // Инициализация InputManager (джойстик)
  InputManager::init(JOY_VRX_PIN, JOY_VRY_PIN, JOY_SW_PIN);
  Serial.println("[OK] Джойстик инициализирован");

  // Инициализация RF433Receiver
  mySwitch.enableReceive(RF_DATA_PIN); // Включаем прием на указанном пине
  RF433Receiver::init(&mySwitch); // Передаем объект mySwitch в менеджер
  Serial.println("[OK] 433MHz приемник инициализирован");

  // Инициализация GateControl (светодиод)
  GateControl::init(LED_PIN);
  Serial.println("[OK] GateControl инициализирован");

  // Инициализация WiFiManager
  WiFiManager::init(&preferences, &server);
  WiFiManager::connectToWiFi();
  Serial.println("[OK] WiFiManager инициализирован");

  // Инициализация GSMManager (опционально)
  // Раскомментируйте, когда GSM модуль будет подключен
  // GSMManager::init(&modem, GSM_TX_PIN, GSM_RX_PIN);
  // Serial.println("[OK] GSM модуль инициализирован");

  // Настройка веб-сервера (если используется)
  WiFiManager::setupWebServer();
  
  Serial.println("=================================");
  Serial.println("Инициализация завершена!");
  Serial.println("=================================");
}

// --- Loop Function ---
void loop() {
  // Обработка джойстика (без обновления меню)
  InputManager::handleInput();
  
  // Обработка 433MHz сигналов
  if (mySwitch.available()) {
    RF433Receiver::handleReceivedCode(
      mySwitch.getReceivedValue(), 
      mySwitch.getReceivedBitlength(), 
      mySwitch.getReceivedProtocol()
    );
    mySwitch.resetAvailable();
  }

  // Обработка веб-запросов
  server.handleClient();

  // Обработка GSM (опционально)
  // Раскомментируйте, когда GSM модуль будет подключен
  // GSMManager::handleGSM();

  // Проверка состояния Wi-Fi и переподключение при необходимости
  WiFiManager::checkConnection();

  // Небольшая задержка для стабильности
  delay(10);
}


