#include <Arduino.h>
#include "WiFiManager.h"
#include "infrastructure/Logger.h"

namespace WiFiManager {
  static Preferences* preferences = nullptr;
  static WebServer* server = nullptr;
  
  static String ssid = "";
  static String password = "";
  static unsigned long lastCheckTime = 0;
  static const unsigned long CHECK_INTERVAL = 30000; // Проверка каждые 30 секунд

  void init(Preferences* prefs, WebServer* webServer) {
    preferences = prefs;
    server = webServer;
    
    // Загрузка сохраненных данных Wi-Fi
    ssid = preferences->getString("wifi_ssid", "");
    password = preferences->getString("wifi_pass", "");
    
    Logger::info("[WiFiManager] WiFi Manager инициализирован");
  }

  void connectToWiFi() {
    if (ssid.length() == 0) {
      Logger::info("[WiFiManager] Нет сохраненных данных WiFi");
      Logger::info("[WiFiManager] Создание точки доступа...");
      
      // Создание точки доступа для настройки
      WiFi.mode(WIFI_AP);
      WiFi.softAP("SmartGate-Config", "12345678");
      
      IPAddress IP = WiFi.softAPIP();
      Logger::info("[WiFiManager] IP точки доступа: " + IP.toString());
      return;
    }
    
    Logger::info("[WiFiManager] Подключение к WiFi...");
    Logger::logf("info", "[WiFiManager] SSID: %s", ssid.c_str());
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Logger::success("[WiFiManager] ✓ Подключено к WiFi!");
      Logger::info("[WiFiManager] IP адрес: " + WiFi.localIP().toString());
    } else {
      Logger::warning("[WiFiManager] ✗ Не удалось подключиться");
      Logger::info("[WiFiManager] Создание точки доступа...");
      
      WiFi.mode(WIFI_AP);
      WiFi.softAP("SmartGate-Config", "12345678");
      
      IPAddress IP = WiFi.softAPIP();
      Logger::info("[WiFiManager] IP точки доступа: " + IP.toString());
    }
  }

  void checkConnection() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastCheckTime < CHECK_INTERVAL) {
      return;
    }
    
    lastCheckTime = currentTime;
    
    if (WiFi.status() != WL_CONNECTED && ssid.length() > 0) {
      Logger::warning("[WiFiManager] Соединение потеряно. Переподключение...");
      connectToWiFi();
    }
  }

  void setupWebServer() {
    if (!server) return;
    
    server->on("/", handleRoot);
    server->on("/save", handleWifiSave);
    server->onNotFound(handleNotFound);
    
    server->begin();
    Logger::success("[WiFiManager] Веб-сервер запущен");
  }

  void handleRoot() {
    String html = R"(
<!DOCTYPE html>
<html lang='ru'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>SmartGate Config</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; }
    h1 { color: #333; }
    input { width: 100%; padding: 10px; margin: 10px 0; box-sizing: border-box; }
    button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; }
    button:hover { background: #45a049; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Настройка WiFi</h1>
    <form action='/save' method='POST'>
      <label>SSID:</label>
      <input type='text' name='ssid' required>
      <label>Пароль:</label>
      <input type='password' name='password' required>
      <button type='submit'>Сохранить</button>
    </form>
  </div>
</body>
</html>
)";
    
    server->send(200, "text/html", html);
    Logger::info("[WiFiManager] Главная страница отправлена");
  }

  void handleWifiSave() {
    if (server->hasArg("ssid") && server->hasArg("password")) {
      ssid = server->arg("ssid");
      password = server->arg("password");
      
      preferences->putString("wifi_ssid", ssid);
      preferences->putString("wifi_pass", password);
      
      Logger::success("[WiFiManager] WiFi настройки сохранены");
      Logger::logf("info", "[WiFiManager] SSID: %s", ssid.c_str());
      
      String html = R"(
<!DOCTYPE html>
<html lang='ru'>
<head>
  <meta charset='UTF-8'>
  <title>Сохранено</title>
  <style>
    body { font-family: Arial; text-align: center; padding: 50px; }
    .message { color: green; font-size: 20px; }
  </style>
</head>
<body>
  <div class='message'>✓ Настройки сохранены!</div>
  <p>Устройство перезагрузится через 3 секунды...</p>
</body>
</html>
)";
      
      server->send(200, "text/html", html);
      
      delay(3000);
      ESP.restart();
    } else {
      server->send(400, "text/plain", "Ошибка: не все поля заполнены");
    }
  }

  void handleNotFound() {
    server->send(404, "text/plain", "Страница не найдена");
  }

  void sendActionToAlice(const String& action) {
    // TODO: Реализация интеграции с Алисой (Домовенок Кузя)
    // Здесь будет код для отправки команды через MQTT или HTTP API
    Logger::logf("info", "[WiFiManager] Отправка действия в Алису: %s", action.c_str());
  }

  bool isConnected() {
    return (WiFi.status() == WL_CONNECTED);
  }
}

