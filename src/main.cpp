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
  bool smsEnabled;
  bool callEnabled;
};

struct WiFiNetwork {
  String ssid;
  int rssi;
  int encryption;
};

// Единая структура состояния системы
struct SystemState {
  std::vector<PhoneEntry> phones;
  std::vector<unsigned long> keys433;
  String wifiSSID;
  String wifiPassword;
  bool wifiConnected;
};

// --- Хранилище данных ---
SystemState systemState;
std::vector<WiFiNetwork> wifiNetworks;

// --- Объявления функций ---
void sendWebSocketEvent(const char* event, const char* data);
void sendLog(String message, const char* type);
void saveSystemState();
void loadSystemState();

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
  
  // Сохраняем учетные данные в состояние системы
  systemState.wifiSSID = ssid;
  systemState.wifiPassword = password;
  
  // Сохраняем состояние
  saveSystemState();
  
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

// Сохранение всего состояния системы в NVS
void saveSystemState() {
  preferences.begin("system", false);
  
  JsonDocument doc;
  
  // Сохраняем телефоны
  JsonArray phonesArray = doc["phones"].to<JsonArray>();
  for (const auto& phone : systemState.phones) {
    JsonObject phoneObj = phonesArray.add<JsonObject>();
    phoneObj["number"] = phone.number;
    phoneObj["smsEnabled"] = phone.smsEnabled;
    phoneObj["callEnabled"] = phone.callEnabled;
  }
  
  // Сохраняем ключи
  JsonArray keysArray = doc["keys"].to<JsonArray>();
  for (const auto& key : systemState.keys433) {
    keysArray.add(key);
  }
  
  // Сохраняем WiFi настройки
  doc["wifi"]["ssid"] = systemState.wifiSSID;
  doc["wifi"]["password"] = systemState.wifiPassword;
  doc["wifi"]["connected"] = systemState.wifiConnected;
  
  String jsonString;
  serializeJson(doc, jsonString);
  preferences.putString("state", jsonString);
  preferences.end();
  
  Serial.println("[NVS] Состояние системы сохранено");
}

// Загрузка всего состояния системы из NVS
void loadSystemState() {
  preferences.begin("system", true);
  String jsonString = preferences.getString("state", "{}");
  preferences.end();
  
  JsonDocument doc;
  deserializeJson(doc, jsonString);
  
  // Очищаем текущее состояние
  systemState.phones.clear();
  systemState.keys433.clear();
  
  // Загружаем телефоны
  if (doc["phones"].is<JsonArray>()) {
    JsonArray phonesArray = doc["phones"];
    for (JsonObject phoneObj : phonesArray) {
      PhoneEntry phone;
      phone.number = phoneObj["number"].as<String>();
      phone.smsEnabled = phoneObj["smsEnabled"].as<bool>();
      phone.callEnabled = phoneObj["callEnabled"].as<bool>();
      systemState.phones.push_back(phone);
    }
  }
  
  // Загружаем ключи
  if (doc["keys"].is<JsonArray>()) {
    JsonArray keysArray = doc["keys"];
    for (unsigned long key : keysArray) {
      if (key > 0) {
        systemState.keys433.push_back(key);
      }
    }
  }
  
  // Загружаем WiFi настройки
  if (doc["wifi"].is<JsonObject>()) {
    JsonObject wifiObj = doc["wifi"];
    systemState.wifiSSID = wifiObj["ssid"].as<String>();
    systemState.wifiPassword = wifiObj["password"].as<String>();
    systemState.wifiConnected = wifiObj["connected"].as<bool>();
  }
  
  Serial.println("[NVS] Состояние системы загружено: " + String(systemState.phones.size()) + " телефонов, " + String(systemState.keys433.size()) + " ключей");
}



// Обработка списка телефонов
void handlePhonesAPI() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc;
    JsonArray phonesArray = doc.to<JsonArray>();
    
    for (const auto& phone : systemState.phones) {
      JsonObject obj = phonesArray.add<JsonObject>();
      obj["id"] = phone.number; // Используем номер как ID
      obj["number"] = phone.number;
      obj["smsEnabled"] = phone.smsEnabled;
      obj["callEnabled"] = phone.callEnabled;
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
    phone.smsEnabled = doc["smsEnabled"].as<bool>();
    phone.callEnabled = doc["callEnabled"].as<bool>();
    systemState.phones.push_back(phone);
    
    // Сохраняем состояние
    saveSystemState();
    
    Serial.println("[API] Добавлен телефон: " + phone.number);
    sendLog("📱 Добавлен телефон: " + phone.number, "success");
    
    // Возвращаем созданный объект с ID (номером)
    JsonDocument responseDoc;
    responseDoc["id"] = phone.number;
    responseDoc["number"] = phone.number;
    responseDoc["smsEnabled"] = phone.smsEnabled;
    responseDoc["callEnabled"] = phone.callEnabled;
    
    String response;
    serializeJson(responseDoc, response);
    server.send(200, "application/json", response);
  }
}

// Обработка обновления настроек телефона
void handlePhoneUpdate() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  String phoneNumber = doc["id"].as<String>();
  
  if (phoneNumber.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Invalid phone number\"}");
    return;
  }
  
  for (auto& phone : systemState.phones) {
    if (phone.number == phoneNumber) {
      if (doc["smsEnabled"].is<bool>()) {
        phone.smsEnabled = doc["smsEnabled"].as<bool>();
      }
      if (doc["callEnabled"].is<bool>()) {
        phone.callEnabled = doc["callEnabled"].as<bool>();
      }
      
      // Сохраняем состояние
      saveSystemState();
      
      Serial.println("[API] Обновлен телефон " + phoneNumber + ": SMS=" + String(phone.smsEnabled) + ", Call=" + String(phone.callEnabled));
      sendLog("📱 Обновлены настройки телефона: " + phone.number, "success");
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  
  server.send(404, "application/json", "{\"error\":\"Phone not found\"}");
}

// Обработка удаления телефонов
void handlePhonesDelete() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  String phoneNumber = doc["id"].as<String>();
  
  for (auto it = systemState.phones.begin(); it != systemState.phones.end(); ++it) {
    if (it->number == phoneNumber) {
      String number = it->number;
      systemState.phones.erase(it);
      
      // Сохраняем состояние
      saveSystemState();
      
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
    
    for (const auto& key : systemState.keys433) {
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
  
  for (auto it = systemState.keys433.begin(); it != systemState.keys433.end(); ++it) {
    if (*it == key) {
      systemState.keys433.erase(it);
      
      // Сохраняем состояние
      saveSystemState();
      
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
  
  // Загрузка состояния системы из постоянной памяти
  loadSystemState();
  

  // Инициализация GateControl
  GateControl::init(LED_PIN);
  Serial.println("[OK] GateControl инициализирован");

  // Инициализация RF433Receiver
  mySwitch.enableReceive(RF_DATA_PIN);
  RF433Receiver::init(&mySwitch);
  Serial.println("[OK] 433MHz приемник инициализирован");

  // Попытка подключения к сохранённой сети
  if (systemState.wifiSSID.length() > 0) {
    Serial.println("[WiFi] Попытка подключения к сохранённой сети: " + systemState.wifiSSID);
    WiFi.begin(systemState.wifiSSID.c_str(), systemState.wifiPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Подключено! IP: " + WiFi.localIP().toString());
      systemState.wifiConnected = true;
    } else {
      Serial.println("[WiFi] Не удалось подключиться к сохранённой сети");
      systemState.wifiConnected = false;
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
  server.on("/api/phones/update", HTTP_PUT, handlePhoneUpdate);
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
      if (systemState.wifiSSID.length() > 0) {
        if (wasConnected) {
          Serial.println("[WiFi] Соединение потеряно, попытка переподключения к: " + systemState.wifiSSID);
          sendLog("⚠️ WiFi отключен, переподключение к " + systemState.wifiSSID + "...", "warning");
          wasConnected = false;
        }
        WiFi.begin(systemState.wifiSSID.c_str(), systemState.wifiPassword.c_str());
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
      // Проверяем, есть ли уже такой ключ
      bool keyExists = false;
      for (const auto& existingKey : systemState.keys433) {
        if (existingKey == key) {
          keyExists = true;
          break;
        }
      }
      
      // Если ключа нет, добавляем его
      if (!keyExists) {
        systemState.keys433.push_back(key);
        saveSystemState();
        Serial.println("[433MHz] Новый ключ добавлен: " + String(key));
        sendLog("🔑 Новый ключ добавлен: " + String(key), "success");
      }
      
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