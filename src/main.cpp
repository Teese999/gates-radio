#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <RCSwitch.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>

// Подключение кастомных модулей
#include "RF433Receiver.h"
#include "GateControl.h"
#include "GSMManager.h"

// --- Константы пинов ---
// Пины для 433MHz приемника
#define RF_DATA_PIN 13 // GPIO13 - Data pin для 433MHz приемника

// Пины для управления воротами
#define LED_PIN     12 // GPIO12 - Управление светодиодом/реле

// Пины для GSM SIM800L (UART2)
#define GSM_RX_PIN  21 // GPIO21 - RX пин для UART2 (подключается к TX GSM модуля)
#define GSM_TX_PIN  16 // GPIO16 - TX пин для UART2 (подключается к RX GSM модуля)

// --- Глобальные объекты ---
Preferences preferences; // Для сохранения данных в NVS
RCSwitch mySwitch = RCSwitch(); // Объект 433MHz приемника
WebServer server(80); // Веб-сервер на порту 80
WebSocketsServer webSocket(81); // WebSocket сервер на порту 81

// --- Структуры данных ---
struct PhoneEntry {
  String number;
  int action; // 0=none, 1=sms, 2=ring, 3=both
};

struct WiFiNetwork {
  String ssid;
  int rssi;
  int encryption;
};

// --- Хранилище данных ---
std::vector<PhoneEntry> phones;
std::vector<unsigned long> keys433;
std::vector<WiFiNetwork> wifiNetworks;

// --- Объявления функций ---
void sendWebSocketEvent(const char* event, const char* data);
void sendLog(String message, const char* type);

// --- WebSocket функции ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WebSocket] Клиент %u отключен\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[WebSocket] Клиент %u подключен из %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        
        // Отправляем приветственное сообщение новому клиенту
        String welcomeMsg = "🌐 Клиент подключен: " + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        sendLog(welcomeMsg, "success");
      }
      break;
    case WStype_TEXT:
      Serial.printf("[WebSocket] Получено: %s\n", payload);
      break;
    default:
      break;
  }
}

void sendWebSocketEvent(const char* event, const char* data) {
  String json = "{\"event\":\"" + String(event) + "\",\"data\":" + String(data) + "}";
  webSocket.broadcastTXT(json);
  Serial.println("[WebSocket] Отправлено: " + json);
}

void sendLog(String message, const char* type = "info") {
  String logData = "{\"message\":\"" + message + "\",\"type\":\"" + String(type) + "\"}";
  sendWebSocketEvent("log", logData.c_str());
}

// --- Функции веб-сервера ---
void handleRoot() {
  if (SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "index.html not found");
  }
}

// Обработка WiFi сканирования
void handleWiFiScan() {
  Serial.println("[API] WiFi Scan запрошен");
  sendLog("🔍 Сканирование WiFi сетей...", "info");
  
  int n = WiFi.scanNetworks();
  wifiNetworks.clear();
  sendLog("📡 Найдено сетей: " + String(n), "success");
  
  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();
  
  for (int i = 0; i < n; i++) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 0 : 1;
    
    WiFiNetwork wifiNet;
    wifiNet.ssid = WiFi.SSID(i);
    wifiNet.rssi = WiFi.RSSI(i);
    wifiNet.encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 0 : 1;
    wifiNetworks.push_back(wifiNet);
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Глобальные переменные для хранения учетных данных WiFi
String savedWiFiSSID = "";
String savedWiFiPassword = "";

// Обработка подключения к WiFi
void handleWiFiConnect() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  String ssid = doc["ssid"].as<String>();
  String password = doc["password"].as<String>();
  
  // Сохраняем учетные данные для автопереподключения
  savedWiFiSSID = ssid;
  savedWiFiPassword = password;
  
  // Сохраняем в Preferences для восстановления после перезагрузки
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  
  Serial.println("[API] Попытка подключения к WiFi: " + ssid);
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  JsonDocument response;
  if (WiFi.status() == WL_CONNECTED) {
    response["success"] = true;
    response["ip"] = WiFi.localIP().toString();
    response["ssid"] = WiFi.SSID();
    response["rssi"] = WiFi.RSSI();
    Serial.println("\n[WiFi] Подключено! IP: " + WiFi.localIP().toString());
    sendLog("✅ WiFi подключен: " + WiFi.localIP().toString(), "success");
    
    // Отправляем обновление статуса через WebSocket
    String wifiStatus = "{\"status\":\"connected\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) + ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    sendWebSocketEvent("wifi_status", wifiStatus.c_str());
  } else {
    response["success"] = false;
    response["error"] = "Connection failed";
    Serial.println("\n[WiFi] Ошибка подключения");
    sendLog("❌ Ошибка подключения к WiFi: " + ssid, "error");
  }
  
  String responseStr;
  serializeJson(response, responseStr);
  server.send(200, "application/json", responseStr);
}

// Обработка списка телефонов
void handlePhonesAPI() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    JsonArray phonesArray = doc.to<JsonArray>();
    
    for (const auto& phone : phones) {
      JsonObject obj = phonesArray.add<JsonObject>();
      obj["number"] = phone.number;
      obj["action"] = phone.action;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } 
  else if (server.method() == HTTP_POST) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    
    PhoneEntry phone;
    phone.number = doc["number"].as<String>();
    phone.action = doc["action"];
    phones.push_back(phone);
    
    Serial.println("[API] Добавлен телефон: " + phone.number);
    sendLog("📱 Добавлен телефон: " + phone.number, "success");
    server.send(200, "application/json", "{\"success\":true}");
  }
}

// Обработка удаления телефонов
void handlePhonesDelete() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  String number = doc["number"].as<String>();
  
  for (auto it = phones.begin(); it != phones.end(); ++it) {
    if (it->number == number) {
      phones.erase(it);
      Serial.println("[API] Удален телефон: " + number);
      sendLog("🗑️ Удален телефон: " + number, "warning");
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  
  server.send(404, "application/json", "{\"success\":false,\"error\":\"Not found\"}");
}

// Обработка списка ключей
void handleKeysAPI() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    JsonArray keysArray = doc.to<JsonArray>();
    
    for (const auto& key : keys433) {
      keysArray.add(key);
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }
}

// Обработка обучения ключа
void handleKeysLearn() {
  Serial.println("[API] Режим обучения ключа активирован");
  sendLog("🎓 Режим обучения: нажмите кнопку на брелке", "warning");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Нажмите кнопку на брелке\"}");
}

// Обработка удаления ключа
void handleKeysDelete() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  unsigned long key = doc["key"];
  
  for (auto it = keys433.begin(); it != keys433.end(); ++it) {
    if (*it == key) {
      keys433.erase(it);
      Serial.println("[API] Удален ключ: " + String(key));
      sendLog("🗑️ Удален ключ: " + String(key), "warning");
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  
  server.send(404, "application/json", "{\"success\":false,\"error\":\"Not found\"}");
}

// Обработка активации ворот
void handleGateTrigger() {
  Serial.println("[API] Активация ворот");
  sendLog("⚡ Сигнал на ворота отправлен", "success");
  GateControl::triggerGatePulse(500);
  server.send(200, "application/json", "{\"success\":true}");
}

// --- Setup Function ---
void setup() {
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("Проект: Умные Ворота (Web + React)");
  Serial.println("ESP32 DevKit 38 pin");
  Serial.println("=================================");
  Serial.println("Запуск системы...");

  // Инициализация Preferences
  preferences.begin("smart-gate", false);
  Serial.println("[OK] Preferences инициализированы");
  
  // Загружаем сохранённые учетные данные WiFi
  savedWiFiSSID = preferences.getString("wifi_ssid", "");
  savedWiFiPassword = preferences.getString("wifi_pass", "");
  if (savedWiFiSSID.length() > 0) {
    Serial.println("[WiFi] Найдены сохранённые данные для: " + savedWiFiSSID);
  }

  // Инициализация GateControl
  GateControl::init(LED_PIN);
  Serial.println("[OK] GateControl инициализирован");

  // Инициализация RF433Receiver
  mySwitch.enableReceive(RF_DATA_PIN);
  RF433Receiver::init(&mySwitch);
  Serial.println("[OK] 433MHz приемник инициализирован");

  // Попытка подключения к сохранённой сети
  if (savedWiFiSSID.length() > 0) {
    Serial.println("[WiFi] Попытка подключения к сохранённой сети: " + savedWiFiSSID);
    WiFi.begin(savedWiFiSSID.c_str(), savedWiFiPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Подключено! IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("[WiFi] Не удалось подключиться к сохранённой сети");
    }
  }
  
  // Создание WiFi Access Point (в любом случае, для конфигурации)
  WiFi.softAP("SmartGate-Config", "12345678");
  Serial.println("[OK] WiFi Access Point создан");
  Serial.println("SSID: SmartGate-Config");
  Serial.println("Password: 12345678");
  Serial.println("IP: 192.168.4.1");
  
  // Инициализация mDNS
  if (MDNS.begin("smartgate")) {
    Serial.println("[OK] mDNS инициализирован");
    Serial.println("Домен: http://smartgate.local");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  } else {
    Serial.println("[ERROR] Ошибка инициализации mDNS");
  }

  // Инициализация SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] Ошибка инициализации SPIFFS");
  } else {
    Serial.println("[OK] SPIFFS инициализирован");
  }
  
  // Инициализация WebSocket сервера (до веб-сервера для логов)
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("[OK] WebSocket сервер запущен на порту 81");

  // Настройка веб-сервера
  server.on("/", handleRoot);
  
  // Универсальный обработчик для статических файлов
  server.onNotFound([]() {
    String path = server.uri();
    
    // Проверяем, что это запрос статического файла
    if (path.startsWith("/static/")) {
      if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        String contentType = "text/plain";
        
        if (path.endsWith(".css")) {
          contentType = "text/css";
        } else if (path.endsWith(".js")) {
          contentType = "application/javascript";
        } else if (path.endsWith(".html")) {
          contentType = "text/html";
        } else if (path.endsWith(".json")) {
          contentType = "application/json";
        }
        
        server.streamFile(file, contentType);
        file.close();
        return;
      }
    }
    
    server.send(404, "text/plain", "Not Found: " + path);
  });
  
  // API endpoints
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWiFiConnect);
  server.on("/api/phones", handlePhonesAPI);
  server.on("/api/phones/delete", HTTP_POST, handlePhonesDelete);
  server.on("/api/keys", handleKeysAPI);
  server.on("/api/keys/learn", HTTP_POST, handleKeysLearn);
  server.on("/api/keys/delete", HTTP_POST, handleKeysDelete);
  server.on("/api/gate/trigger", HTTP_POST, handleGateTrigger);
  
  server.begin();
  Serial.println("[OK] Веб-сервер запущен на порту 80");

  // Инициализация GSMManager (опционально)
  // Раскомментируйте, когда GSM модуль будет подключен
  // GSMManager::init(&modem, GSM_TX_PIN, GSM_RX_PIN);
  // Serial.println("[OK] GSM модуль инициализирован");
  
  Serial.println("=================================");
  Serial.println("Инициализация завершена!");
  Serial.println("Подключитесь к WiFi: SmartGate-Config");
  Serial.println("Откройте браузер: http://smartgate.local");
  Serial.println("Или по IP: http://192.168.4.1");
  Serial.println("=================================");
  
  // Отправляем приветственные логи через WebSocket (будут доставлены при подключении)
  delay(1000);
  sendLog("✅ Система запущена", "success");
  sendLog("📡 WiFi AP: SmartGate-Config", "info");
  sendLog("🌐 Адрес: http://smartgate.local", "info");
  sendLog("🔌 433MHz приемник активен", "info");
}

// --- Loop Function ---
void loop() {
  // Обработка веб-запросов
  server.handleClient();
  
  // Обработка WebSocket
  webSocket.loop();
  
  // Периодическая отправка статуса WiFi и переподключение (каждые 5 секунд)
  static unsigned long lastWiFiUpdate = 0;
  static bool wasConnected = false;
  
  if (millis() - lastWiFiUpdate > 5000) {
    lastWiFiUpdate = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      // Отправляем статус подключения
      String wifiStatus = "{\"status\":\"connected\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) + ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
      sendWebSocketEvent("wifi_status", wifiStatus.c_str());
      
      if (!wasConnected) {
        wasConnected = true;
        Serial.println("[WiFi] Успешное подключение к: " + WiFi.SSID());
      }
    } else {
      // Если было подключение, пытаемся переподключиться
      if (savedWiFiSSID.length() > 0) {
        if (wasConnected) {
          Serial.println("[WiFi] Соединение потеряно, попытка переподключения к: " + savedWiFiSSID);
          sendLog("⚠️ WiFi отключен, переподключение к " + savedWiFiSSID + "...", "warning");
          wasConnected = false;
        }
        WiFi.begin(savedWiFiSSID.c_str(), savedWiFiPassword.c_str());
      }
      
      // Отправляем статус отключения
      String wifiStatus = "{\"status\":\"disconnected\"}";
      sendWebSocketEvent("wifi_status", wifiStatus.c_str());
    }
  }

  // Обработка 433MHz сигналов
  if (mySwitch.available()) {
    unsigned long key = mySwitch.getReceivedValue();
    int bitLength = mySwitch.getReceivedBitlength();
    int protocol = mySwitch.getReceivedProtocol();
    
    if (key != 0) {
      RF433Receiver::handleReceivedCode(key, bitLength, protocol);
      
      // Отправляем событие в React через WebSocket
      String keyData = "{\"key\":" + String(key) + 
                       ",\"bitLength\":" + String(bitLength) + 
                       ",\"protocol\":" + String(protocol) + 
                       ",\"timestamp\":" + String(millis()) + "}";
      sendWebSocketEvent("key_received", keyData.c_str());
      
      // Логируем только в Serial, чтобы не дублировать с фронтом
      Serial.println("[433MHz] Ключ: " + String(key) + ", Протокол: " + String(protocol));
    }
    
    mySwitch.resetAvailable();
  }

  // Обработка GSM (опционально)
  // Раскомментируйте, когда GSM модуль будет подключен
  // GSMManager::handleGSM();
}