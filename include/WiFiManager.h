#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

/**
 * Модуль WiFiManager.h
 * Управление Wi-Fi подключением, сохранение учетных данных, веб-сервер для настройки.
 * Интеграция с Алисой (через MQTT или HTTP запросы к облаку).
 */
namespace WiFiManager {
  /**
   * Инициализация WiFi менеджера
   * @param prefs - указатель на объект Preferences
   * @param webServer - указатель на объект WebServer
   */
  void init(Preferences* prefs, WebServer* webServer);
  
  /**
   * Подключение к сохраненной Wi-Fi сети
   * Если нет сохраненных данных, создает точку доступа для настройки
   */
  void connectToWiFi();
  
  /**
   * Проверка состояния подключения
   * При разрыве соединения пытается переподключиться
   */
  void checkConnection();
  
  /**
   * Настройка веб-сервера
   * Регистрация обработчиков запросов
   */
  void setupWebServer();
  
  /**
   * Обработчик главной страницы
   */
  void handleRoot();
  
  /**
   * Обработчик сохранения Wi-Fi настроек
   */
  void handleWifiSave();
  
  /**
   * Обработчик для несуществующих страниц
   */
  void handleNotFound();
  
  /**
   * Отправка команды для Алисы (Домовенок Кузя)
   * @param action - действие для отправки
   */
  void sendActionToAlice(const String& action);
  
  /**
   * Проверка состояния подключения к Wi-Fi
   * @return true если подключено, false если нет
   */
  bool isConnected();
}

#endif // WIFI_MANAGER_H


