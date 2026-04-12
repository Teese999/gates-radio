#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <algorithm>
#include <vector>
#include <nvs_flash.h>
#include <nvs.h>

// Подключение кастомных модулей
#include "CC1101Manager.h"
#include "GateControl.h"
#include "GSMManager.h"
#include "infrastructure/Logger.h"

// --- Константы пинов ---
// Пины для CC1101 (SPI + управляющие)
#define CC1101_CS   5  // GPIO5  - Chip Select (CSN)
#define CC1101_GDO0 4  // GPIO4  - Data Output (основной)
#define CC1101_GDO2 2  // GPIO2  - Преамбула (для нестандартных брелков)
// SPI пины (стандартные для ESP32):
// SCK  - GPIO18
// MISO - GPIO19
// MOSI - GPIO23

// Пины для управления воротами
#define LED_PIN     12 // GPIO12 - Управление светодиодом/реле

// Пины для GSM SIM800L (UART2)
#define GSM_RX_PIN  16 // GPIO16 - RX пин для UART2 (подключается к TX GSM модуля)
#define GSM_TX_PIN  17 // GPIO17 - TX пин для UART2 (подключается к RX GSM модуля)

// --- Глобальные объекты ---
Preferences preferences; // Для сохранения данных в NVS
WebServer server(80); // Веб-сервер на порту 80
WebSocketsServer webSocket(81); // WebSocket сервер на порту 81

// --- Структуры данных ---
struct PhoneEntry {
  String number;
  bool smsEnabled;
  bool callEnabled;
};

struct KeyEntry {
  uint32_t code;              // Младшие 32 бита кода (для совместимости)
  String name;                // Имя ключа
  bool enabled;               // Активен ли ключ
  String protocol;            // Протокол (CAME, Keeloq, Princeton и т.д.)
  String bitString;           // Полная битовая строка (для точного сравнения)
  int bitLength;              // Количество бит
  float te;                   // Базовый период (Time Element) в мкс
  float frequency;            // Частота в МГц
  String modulation;          // Модуляция (AM650, FM476 и т.д.)
  String rawData;             // RAW данные (для отображения)
  int rssi;                   // RSSI при обучении
  unsigned long timestamp;    // Время добавления
};

struct WiFiNetwork {
  String ssid;
  int rssi;
  int encryption;
};

// Структура для отслеживания распознавания ключей (верификация сигнала)
struct KeyRecognition {
  uint32_t code;
  String protocol;
  String bitString;
  int repeatCount;
  unsigned long firstSeen;
  unsigned long lastSeen;
  float frequency;
  int requiredRepeats;
  int lastRssi;
  bool fullDecode;
  float te;
  
  KeyRecognition()
    : code(0), repeatCount(0), firstSeen(0), lastSeen(0), frequency(0.0f),
      requiredRepeats(2), lastRssi(0), fullDecode(false), te(0.0f) {}
};

// Единая структура состояния системы
struct SystemState {
  std::vector<PhoneEntry> phones;
  std::vector<KeyEntry> keys433;
  String wifiSSID;
  String wifiPassword;
  bool wifiConnected;
  bool learningMode;
  float currentFrequency;
  float bitRate;
  float freqDeviation;
  float rxBandwidth;
  int outputPower;
  uint32_t gateOpenCount;

  // Временное хранилище для верификации сигналов
  std::vector<KeyRecognition> pendingRecognitions;
};

// --- Ring-buffer лог файл ---
// Хранит последние N строк в SPIFFS, автоматически удаляет старые
namespace RingLog {
  static const char* LOG_FILE = "/log.txt";
  static const size_t MAX_LOG_SIZE = 32768; // 32KB макс размер файла
  static const size_t TRIM_TO_SIZE = 24576; // Обрезаем до 24KB когда превышен лимит

  void append(const char* message) {
    File f = SPIFFS.open(LOG_FILE, "a");
    if (!f) return;

    // Формируем строку лога с uptime
    unsigned long sec = millis() / 1000;
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "[%luh%02lum] ", sec / 3600, (sec % 3600) / 60);
    f.print(timeBuf);
    f.println(message);
    size_t sz = f.size();
    f.close();

    // Если файл превысил лимит — обрезаем (удаляем начало, оставляем конец)
    if (sz > MAX_LOG_SIZE) {
      File rf = SPIFFS.open(LOG_FILE, "r");
      if (!rf) return;
      // Пропускаем начало файла
      rf.seek(sz - TRIM_TO_SIZE);
      // Ищем начало следующей строки
      while (rf.available()) {
        if (rf.read() == '\n') break;
      }
      String tail = rf.readString();
      rf.close();
      // Перезаписываем файл
      File wf = SPIFFS.open(LOG_FILE, "w");
      if (wf) {
        wf.print("--- лог обрезан ---\n");
        wf.print(tail);
        wf.close();
      }
    }
  }
}

// --- Хранилище данных ---
SystemState systemState;
std::vector<WiFiNetwork> wifiNetworks;

// История обнаруженных сигналов (для remove duplicates)
struct RecentDetection {
  String protocol;
  uint32_t code;
  String bitString;
  uint32_t hash;
  unsigned long firstSeen;
  unsigned long lastSeen;
  int count;
};

static std::vector<RecentDetection> detectionHistory;

// --- Объявления функций ---
void sendWebSocketEvent(const char* event, const char* data);
void sendLog(String message, const char* type);
void saveSystemState();
void loadSystemState();

// Функция улучшенного сравнения ключей (как во Flipper Zero)
bool isKeyMatch(const KeyEntry& saved, const ReceivedKey& received, const String& receivedBitString, int receivedBitLength, float receivedTe);
// Функция сравнения битовых строк с допуском
bool compareBitStrings(const String& str1, const String& str2, float minSimilarity = 0.95f);
// Функция верификации сигнала (требует повторения)
bool verifyKeySignal(const ReceivedKey& received, const String& bitString, int bitLength, float te, bool learningMode = false);
// Очистка истории обнаруженных сигналов
void cleanupDetectionHistory();
// Проверка дубликатов (как remove duplicate во Flipper)
bool isDuplicateForDisplay(const ReceivedKey& key);

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

// Инициализация раздела userdata (вызывается один раз в setup)
void initUserDataPartition() {
  esp_err_t err = nvs_flash_init_partition("userdata");
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // Раздел повреждён — форматируем и инициализируем заново
    Serial.println("[NVS] Форматирование раздела userdata...");
    nvs_flash_erase_partition("userdata");
    err = nvs_flash_init_partition("userdata");
  }
  if (err != ESP_OK) {
    Serial.printf("[NVS] ОШИБКА инициализации userdata: %s\n", esp_err_to_name(err));
  } else {
    Serial.println("[NVS] Раздел userdata инициализирован");
  }
}

// Сохранение строки в раздел userdata
bool saveToUserData(const char* key, const String& value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open_from_partition("userdata", "state", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    Serial.printf("[NVS] Ошибка открытия userdata: %s\n", esp_err_to_name(err));
    return false;
  }
  err = nvs_set_blob(handle, key, value.c_str(), value.length() + 1);
  if (err == ESP_OK) {
    nvs_commit(handle);
  }
  nvs_close(handle);
  return err == ESP_OK;
}

// Чтение строки из раздела userdata
String loadFromUserData(const char* key) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open_from_partition("userdata", "state", NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return "{}";
  }
  size_t required_size = 0;
  err = nvs_get_blob(handle, key, NULL, &required_size);
  if (err != ESP_OK || required_size == 0) {
    nvs_close(handle);
    return "{}";
  }
  char* buf = (char*)malloc(required_size);
  if (!buf) {
    nvs_close(handle);
    return "{}";
  }
  err = nvs_get_blob(handle, key, buf, &required_size);
  String result = (err == ESP_OK) ? String(buf) : String("{}");
  free(buf);
  nvs_close(handle);
  return result;
}

// Сохранение всего состояния системы в раздел userdata
void saveSystemState() {
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
    JsonObject keyObj = keysArray.add<JsonObject>();
    keyObj["code"] = key.code;
    keyObj["name"] = key.name;
    keyObj["enabled"] = key.enabled;
    keyObj["protocol"] = key.protocol;
    keyObj["bitString"] = key.bitString;
    keyObj["bitLength"] = key.bitLength;
    keyObj["te"] = key.te;
    keyObj["frequency"] = key.frequency;
    keyObj["modulation"] = key.modulation;
    keyObj["rawData"] = key.rawData;
    keyObj["rssi"] = key.rssi;
    keyObj["timestamp"] = key.timestamp;
  }

  // Сохраняем WiFi настройки
  doc["wifi"]["ssid"] = systemState.wifiSSID;
  doc["wifi"]["password"] = systemState.wifiPassword;
  doc["wifi"]["connected"] = systemState.wifiConnected;

  // Сохраняем состояние режима обучения
  doc["learningMode"] = systemState.learningMode;

  // Сохраняем частоту
  doc["frequency"] = systemState.currentFrequency;

  // Сохраняем CC1101 настройки
  doc["bitRate"] = systemState.bitRate;
  doc["freqDeviation"] = systemState.freqDeviation;
  doc["rxBandwidth"] = systemState.rxBandwidth;
  doc["outputPower"] = systemState.outputPower;

  // Сохраняем счётчик открытий
  doc["gateOpenCount"] = systemState.gateOpenCount;

  String jsonString;
  serializeJson(doc, jsonString);

  if (saveToUserData("state", jsonString)) {
    Serial.println("[NVS] Состояние сохранено в userdata (" + String(jsonString.length()) + " байт)");
  } else {
    Serial.println("[NVS] ОШИБКА сохранения в userdata!");
  }
}

// Функция сравнения битовых строк с допуском
bool compareBitStrings(const String& str1, const String& str2, float minSimilarity) {
  if (str1.length() == 0 || str2.length() == 0) {
    return false;
  }
  
  int minLen = min(str1.length(), str2.length());
  int maxLen = max(str1.length(), str2.length());
  
  if (minLen == 0) return false;
  
  // Сравниваем по минимальной длине
  int matches = 0;
  for (int i = 0; i < minLen; i++) {
    if (str1[i] == str2[i]) {
      matches++;
    }
  }
  
  float similarity = static_cast<float>(matches) / maxLen;
  return similarity >= minSimilarity;
}

// Функция улучшенного сравнения ключей
// Проблема: один и тот же пульт может декодироваться как разные протоколы
// (CAME 24-bit vs X10 20-bit и т.д.) из-за нестабильности декодера.
// Решение: мягкое сравнение — приоритет bitString/code, протокол вторичен.
bool isKeyMatch(const KeyEntry& saved, const ReceivedKey& received, const String& receivedBitString, int receivedBitLength, float receivedTe) {
  // 1. Частота должна совпадать (допуск ±1 МГц)
  float currentFreq = CC1101Manager::getFrequency();
  float freqDiff = (saved.frequency > currentFreq) ?
                   (saved.frequency - currentFreq) :
                   (currentFreq - saved.frequency);
  if (freqDiff > 1.0f) {
    return false;
  }

  // 2. Точное совпадение: протокол + bitString
  if (saved.protocol == received.protocol &&
      saved.bitString.length() > 0 && receivedBitString.length() > 0) {
    if (saved.bitLength <= 32) {
      if (saved.bitString == receivedBitString) return true;
    } else {
      if (compareBitStrings(saved.bitString, receivedBitString, 0.95f)) return true;
    }
  }

  // 3. Совпадение по коду (протокол может быть другим!)
  // Один пульт CAME может детектиться то как CAME то как X10 — код при этом частично совпадает
  if (saved.code != 0 && saved.code == received.code) {
    // TE должен быть примерно одинаковым (допуск ±40%)
    if (saved.te > 0 && receivedTe > 0) {
      float teDiff = (saved.te > receivedTe) ? (saved.te / receivedTe) : (receivedTe / saved.te);
      if (teDiff > 1.4f) return false;
    }
    return true;
  }

  // 4. Совпадение по bitString содержанию (разные протоколы, одни данные)
  // Если одна bitString содержит другую — это тот же пульт, просто декодировалось
  // разное количество бит (напр. CAME 24 vs X10 20 — первые 20 бит одинаковые)
  if (saved.bitString.length() >= 12 && receivedBitString.length() >= 12) {
    const String& shorter = (saved.bitString.length() <= receivedBitString.length()) ? saved.bitString : receivedBitString;
    const String& longer = (saved.bitString.length() > receivedBitString.length()) ? saved.bitString : receivedBitString;

    // Проверяем что короткая строка является подстрокой длинной (начиная с начала или конца)
    if (longer.startsWith(shorter) || longer.endsWith(shorter)) {
      // Дополнительно проверяем TE (допуск ±40%)
      if (saved.te > 0 && receivedTe > 0) {
        float teDiff = (saved.te > receivedTe) ? (saved.te / receivedTe) : (receivedTe / saved.te);
        if (teDiff > 1.4f) return false;
      }
      Serial.printf("[KeyMatch] Совпадение по bitString подстроке: saved=%s recv=%s\n",
                    saved.protocol.c_str(), received.protocol.c_str());
      return true;
    }
  }

  return false;
}

// Функция верификации сигнала (адаптивная)
// Возвращает true, если сигнал подтвержден достаточным количеством повторений
// В режиме обучения (learningMode) сигнал принимается сразу, как во Flipper Zero
bool verifyKeySignal(const ReceivedKey& received, const String& bitString, int bitLength, float te, bool learningMode) {
  if (learningMode) {
    return true;
  }

  const unsigned long VERIFICATION_WINDOW_MS = 1500; // Временное окно для повторений
  const unsigned long RESET_TIMEOUT_MS = 2500;        // Максимальное время ожидания между сериями
  const int MAX_REPEATS = 5;

  bool hasBitString = (bitLength > 0 && bitString.length() > 0);
  bool isFullDecode = (received.protocol != "RAW/Unknown" && hasBitString);
  bool isLongProtocol = (bitLength >= 56);
  bool isVeryLongProtocol = (bitLength >= 80);
  bool isRaw = (received.protocol == "RAW/Unknown");

  // Определяем, сколько повторов требуется для подтверждения
  int requiredRepeats = 2; // Базовое значение

  if (isFullDecode && received.rssi > -68 && !isLongProtocol) {
    requiredRepeats = 1;
  }

  if (isRaw || received.rssi < -85) {
    requiredRepeats = std::max(requiredRepeats, 3);
  }

  if (isLongProtocol && received.rssi < -80) {
    requiredRepeats = std::max(requiredRepeats, 3);
  }

  if (isVeryLongProtocol) {
    requiredRepeats = std::max(requiredRepeats, 3);
  }

  requiredRepeats = std::min(requiredRepeats, MAX_REPEATS);

  // Если достаточно одного повторения - подтверждаем немедленно
  if (requiredRepeats <= 1) {
    return true;
  }

  unsigned long now = millis();

  // Ищем существующее распознавание
  KeyRecognition* recognition = nullptr;
  for (auto& rec : systemState.pendingRecognitions) {
    if (rec.protocol == received.protocol &&
        rec.code == received.code &&
        (bitString.length() == 0 || compareBitStrings(rec.bitString, bitString, 0.95f))) {
      recognition = &rec;
      break;
    }
  }

  if (recognition == nullptr) {
    // Создаем новую запись о распознавании
    KeyRecognition newRec;
    newRec.code = received.code;
    newRec.protocol = received.protocol;
    newRec.bitString = bitString;
    newRec.repeatCount = 1;
    newRec.firstSeen = now;
    newRec.lastSeen = now;
    newRec.frequency = CC1101Manager::getFrequency();
    newRec.requiredRepeats = requiredRepeats;
    newRec.lastRssi = received.rssi;
    newRec.fullDecode = isFullDecode;
    newRec.te = te;

    systemState.pendingRecognitions.push_back(newRec);
    return false; // Нужны дополнительные повторы
  }

  // Обновляем существующую запись
  // Если слишком много времени прошло между повторениями — начинаем заново
  if ((now - recognition->lastSeen) > RESET_TIMEOUT_MS) {
    recognition->repeatCount = 1;
    recognition->firstSeen = now;
    recognition->requiredRepeats = requiredRepeats;
  } else {
    // Накапливаем повторы
    recognition->repeatCount++;
    // Не даем выйти за пределы MAX_REPEATS
    if (recognition->repeatCount > MAX_REPEATS) {
      recognition->repeatCount = MAX_REPEATS;
    }
    // Если новый сигнал сильнее/качественнее, можем снизить требуемое число повторов
    if (requiredRepeats < recognition->requiredRepeats) {
      recognition->requiredRepeats = requiredRepeats;
    }
  }

  recognition->lastSeen = now;
  recognition->lastRssi = received.rssi;
  recognition->fullDecode = recognition->fullDecode || isFullDecode;
  recognition->te = te;

  // Если недостаточно повторов в текущем окне — продолжаем ждать
  if ((now - recognition->firstSeen) > VERIFICATION_WINDOW_MS &&
      recognition->repeatCount < recognition->requiredRepeats) {
    // Окно закончилось — начинаем новую серию с текущего сигнала
    recognition->repeatCount = 1;
    recognition->firstSeen = now;
    recognition->requiredRepeats = requiredRepeats;
    recognition->bitString = bitString;
    recognition->fullDecode = isFullDecode;
    return false;
  }

  // Подтверждаем, если достигли требуемого количества повторов
  if (recognition->repeatCount >= recognition->requiredRepeats) {
    // Удаляем запись
    systemState.pendingRecognitions.erase(
      std::remove_if(
        systemState.pendingRecognitions.begin(),
        systemState.pendingRecognitions.end(),
        [&](const KeyRecognition& rec) {
          return rec.code == recognition->code && rec.protocol == recognition->protocol;
        }
      ),
      systemState.pendingRecognitions.end()
    );

    Serial.printf("[Verify] ✅ Подтверждено: протокол=%s, повторов=%d (требовалось %d), RSSI=%d dBm\n",
                  received.protocol.c_str(), recognition->repeatCount, recognition->requiredRepeats, received.rssi);
    return true;
  }

  return false;
}

// Очистка устаревших распознаваний
void cleanupOldRecognitions() {
  const unsigned long CLEANUP_TIMEOUT_MS = 5000; // 5 секунд
  unsigned long now = millis();
  
  systemState.pendingRecognitions.erase(
    std::remove_if(
      systemState.pendingRecognitions.begin(),
      systemState.pendingRecognitions.end(),
      [now](const KeyRecognition& rec) {
        return (now - rec.lastSeen) > CLEANUP_TIMEOUT_MS;
      }
    ),
    systemState.pendingRecognitions.end()
  );
}

// Очистка истории обнаруженных сигналов (remove duplicates)
void cleanupDetectionHistory() {
  const unsigned long HISTORY_TIMEOUT_MS = 60000; // 60 секунд
  unsigned long now = millis();

  detectionHistory.erase(
    std::remove_if(
      detectionHistory.begin(),
      detectionHistory.end(),
      [now, HISTORY_TIMEOUT_MS](const RecentDetection& rec) {
        return (now - rec.lastSeen) > HISTORY_TIMEOUT_MS;
      }
    ),
    detectionHistory.end()
  );

  const size_t MAX_HISTORY_SIZE = 120;
  if (detectionHistory.size() > MAX_HISTORY_SIZE) {
    detectionHistory.erase(
      detectionHistory.begin(),
      detectionHistory.begin() + (detectionHistory.size() - MAX_HISTORY_SIZE)
    );
  }
}

// Проверка на дубликаты (аналог remove duplicate во Flipper)
bool isDuplicateForDisplay(const ReceivedKey& key) {
  unsigned long now = millis();

  for (auto& rec : detectionHistory) {
    bool sameProtocol = (rec.protocol == key.protocol);
    bool matchByBits = false;
    bool matchByCode = false;

    if (sameProtocol) {
      if (key.bitString.length() > 0 && rec.bitString.length() > 0) {
        matchByBits = (rec.bitString == key.bitString);
      }
      if (!matchByBits && key.bitString.length() == 0 && rec.bitString.length() == 0) {
        matchByCode = (rec.code == key.code);
      }
    }

    bool rawMatch = (key.protocol == "RAW/Unknown" && rec.protocol == "RAW/Unknown" && key.hash != 0 && rec.hash == key.hash);

    if ((sameProtocol && (matchByBits || matchByCode)) || rawMatch) {
      rec.lastSeen = now;
      rec.count++;
      rec.hash = key.hash;
      return true;
    }
  }

  RecentDetection rec;
  rec.protocol = key.protocol;
  rec.code = key.code;
  rec.bitString = key.bitString;
  rec.hash = key.hash;
  rec.firstSeen = now;
  rec.lastSeen = now;
  rec.count = 1;
  detectionHistory.push_back(rec);

  return false;
}

// Загрузка всего состояния системы из раздела userdata
void loadSystemState() {
  String jsonString = loadFromUserData("state");
  
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
    for (JsonObject keyObj : keysArray) {
      KeyEntry key;
      key.code = keyObj["code"].as<uint32_t>();
      key.name = keyObj["name"].as<String>();
      key.enabled = keyObj["enabled"].as<bool>();
      key.protocol = keyObj["protocol"] | "RAW/Custom";
      key.bitString = keyObj["bitString"] | "";  // Новое поле
      key.bitLength = keyObj["bitLength"] | 0;   // Новое поле
      key.te = keyObj["te"] | 400.0f;            // Новое поле (дефолт 400 мкс)
      key.frequency = keyObj["frequency"] | 433.92;
      key.modulation = keyObj["modulation"] | "ASK/OOK";
      key.rawData = keyObj["rawData"] | "";
      key.rssi = keyObj["rssi"] | 0;
      key.timestamp = keyObj["timestamp"].as<unsigned long>();
      
      // Миграция старых ключей: если нет bitLength, пытаемся определить из протокола
      if (key.bitLength == 0 && key.protocol != "RAW/Custom" && key.protocol != "RAW/Unknown") {
        // Пытаемся определить количество бит из протокола
        if (key.protocol.indexOf("12") >= 0) {
          key.bitLength = 12;
        } else if (key.protocol.indexOf("24") >= 0) {
          key.bitLength = 24;
        } else if (key.protocol.indexOf("64") >= 0 || key.protocol == "Keeloq") {
          key.bitLength = 64;
        } else if (key.protocol.indexOf("56") >= 0) {
          key.bitLength = 56;
        } else {
          key.bitLength = 24; // Дефолт
        }
      }
      
      if (key.code > 0) {
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
  
  // Загружаем состояние режима обучения
  systemState.learningMode = doc["learningMode"].as<bool>();
  
  // Загружаем частоту
  systemState.currentFrequency = doc["frequency"] | 433.92;

  // Загружаем CC1101 настройки
  systemState.bitRate = doc["bitRate"] | 3.79f;
  systemState.freqDeviation = doc["freqDeviation"] | 5.2f;
  systemState.rxBandwidth = doc["rxBandwidth"] | 58.0f;
  systemState.outputPower = doc["outputPower"] | 10;

  // Загружаем счётчик открытий
  systemState.gateOpenCount = doc["gateOpenCount"] | 0;
  
  // Принудительно сбрасываем режим обучения при загрузке
  systemState.learningMode = false;
  
  Serial.println("[NVS] Состояние системы загружено: " + String(systemState.phones.size()) + " телефонов, " + String(systemState.keys433.size()) + " ключей");
  Serial.println("[NVS] Частота: " + String(systemState.currentFrequency) + " МГц");
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
      JsonObject keyObj = keysArray.add<JsonObject>();
      keyObj["code"] = key.code;
      keyObj["name"] = key.name;
      keyObj["enabled"] = key.enabled;
      keyObj["protocol"] = key.protocol;
      keyObj["bitString"] = key.bitString;
      keyObj["bitLength"] = key.bitLength;
      keyObj["te"] = key.te;
      keyObj["frequency"] = key.frequency;
      keyObj["modulation"] = key.modulation;
      keyObj["rawData"] = key.rawData;
      keyObj["rssi"] = key.rssi;
      keyObj["timestamp"] = key.timestamp;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }
}

// Обработка обучения ключа
void handleKeysLearn() {
  Serial.println("[API] Получен запрос на обучение ключа");
  
  systemState.learningMode = true;
  CC1101Manager::resetReceived();
  
  // Сохраняем состояние
  saveSystemState();
  
  Serial.println("[API] Режим обучения ключа активирован");
  Serial.println("[API] learningMode = " + String(systemState.learningMode));
  sendLog("🎓 Режим обучения: нажмите кнопку на брелке", "warning");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Нажмите кнопку на брелке\"}");
}

// Обработка остановки режима обучения
void handleKeysStop() {
  systemState.learningMode = false;
  
  // Сохраняем состояние
  saveSystemState();
  
  Serial.println("[API] Режим обучения ключа остановлен");
  sendLog("🛑 Режим обучения остановлен", "warning");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Режим обучения остановлен\"}");
}

// Обработка получения статуса режима обучения
void handleKeysStatus() {
  Serial.println("[API] Запрос статуса режима обучения");
  Serial.println("[API] learningMode = " + String(systemState.learningMode));
  
  JsonDocument doc;
  doc["learningMode"] = systemState.learningMode;
  doc["keyCount"] = systemState.keys433.size();
  
  String response;
  serializeJson(doc, response);
  Serial.println("[API] Отправляем статус: " + response);
  server.send(200, "application/json", response);
}

// Обработка удаления ключа
void handleKeysDelete() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  unsigned long keyCode = doc["code"].as<unsigned long>();
  
  for (auto it = systemState.keys433.begin(); it != systemState.keys433.end(); ++it) {
    if (it->code == keyCode) {
      String keyName = it->name;
      systemState.keys433.erase(it);
      
      // Сохраняем состояние
      saveSystemState();
      
      Serial.println("[API] Удален ключ: " + String(keyCode) + " (" + keyName + ")");
      sendLog("🗑️ Удален ключ: " + keyName, "warning");
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  
  server.send(404, "application/json", "{\"success\":false,\"error\":\"Not found\"}");
}

// Обработка обновления настроек ключа
void handleKeyUpdate() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  unsigned long keyCode = doc["code"].as<unsigned long>();
  
  if (keyCode == 0) {
    server.send(400, "application/json", "{\"error\":\"Invalid key code\"}");
    return;
  }
  
  for (auto& key : systemState.keys433) {
    if (key.code == keyCode) {
      if (doc["enabled"].is<bool>()) {
        key.enabled = doc["enabled"].as<bool>();
      }
      if (doc["name"].is<String>()) {
        key.name = doc["name"].as<String>();
      }
      
      // Сохраняем состояние
      saveSystemState();
      
      Serial.println("[API] Обновлен ключ " + String(keyCode) + ": enabled=" + String(key.enabled) + ", name=" + key.name);
      sendLog("🔑 Обновлены настройки ключа: " + key.name, "success");
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  
  server.send(404, "application/json", "{\"error\":\"Key not found\"}");
}

// Обработка активации ворот
void handleGateTrigger() {
  Serial.println("[API] Активация ворот");
  sendLog("⚡ Сигнал на ворота отправлен", "success");
  GateControl::triggerGatePulse(500);
  systemState.gateOpenCount++;
  saveSystemState();
  RingLog::append("Ворота активированы (API)");
  server.send(200, "application/json", "{\"success\":true}");
}

// Системная информация
void handleSystemInfo() {
  JsonDocument doc;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["totalHeap"] = ESP.getHeapSize();
  doc["rssi"] = CC1101Manager::getRSSI();
  doc["firmware"] = "v1.0";
  doc["openCount"] = systemState.gateOpenCount;
  doc["spiffsFree"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();
  doc["spiffsTotal"] = SPIFFS.totalBytes();
  doc["keyCount"] = systemState.keys433.size();
  doc["phoneCount"] = systemState.phones.size();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Получение лог-файла
void handleLogFile() {
  if (SPIFFS.exists(RingLog::LOG_FILE)) {
    File f = SPIFFS.open(RingLog::LOG_FILE, "r");
    server.streamFile(f, "text/plain");
    f.close();
  } else {
    server.send(200, "text/plain", "");
  }
}

// Обработка получения частоты
void handleFrequencyGet() {
  JsonDocument doc;
  doc["frequency"] = CC1101Manager::getFrequency();
  doc["rssi"] = CC1101Manager::getRSSI();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Обработка установки частоты
void handleFrequencySet() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  
  float frequency = doc["frequency"].as<float>();
  
  if (frequency < 300.0 || frequency > 928.0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Частота должна быть в диапазоне 300-928 МГц\"}");
    return;
  }
  
  Serial.println("[API] Установка частоты: " + String(frequency) + " МГц");
  
  if (CC1101Manager::setFrequency(frequency)) {
    systemState.currentFrequency = frequency;
    saveSystemState();
    
    sendLog("📡 Частота изменена на " + String(frequency) + " МГц", "success");
    
    JsonDocument response;
    response["success"] = true;
    response["frequency"] = frequency;
    
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);
  } else {
    sendLog("❌ Ошибка изменения частоты", "error");
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Ошибка установки частоты\"}");
  }
}

// Обработка получения конфигурации CC1101
void handleCC1101Config() {
  JsonDocument doc;
  doc["frequency"] = CC1101Manager::getFrequency();
  doc["rssi"] = CC1101Manager::getRSSI();
  doc["status"] = "active";
  doc["bitRate"] = systemState.bitRate;
  doc["frequencyDeviation"] = systemState.freqDeviation;
  doc["rxBandwidth"] = systemState.rxBandwidth;
  doc["outputPower"] = systemState.outputPower;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Сохранение всех настроек CC1101
void handleCC1101Settings() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));

  bool ok = true;
  JsonDocument resp;

  // Частота
  if (doc.containsKey("frequency")) {
    float freq = doc["frequency"].as<float>();
    if (freq >= 300.0 && freq <= 928.0) {
      if (CC1101Manager::setFrequency(freq)) {
        systemState.currentFrequency = freq;
      } else { ok = false; }
    }
  }

  // Битрейт
  if (doc.containsKey("bitRate")) {
    float br = doc["bitRate"].as<float>();
    if (CC1101Manager::setBitRate(br)) {
      systemState.bitRate = br;
    }
  }

  // Девиация
  if (doc.containsKey("frequencyDeviation")) {
    float fd = doc["frequencyDeviation"].as<float>();
    if (CC1101Manager::setFrequencyDeviation(fd)) {
      systemState.freqDeviation = fd;
    }
  }

  // RX bandwidth
  if (doc.containsKey("rxBandwidth")) {
    float rxBw = doc["rxBandwidth"].as<float>();
    if (CC1101Manager::setRxBandwidth(rxBw)) {
      systemState.rxBandwidth = rxBw;
    }
  }

  // Output power (через RadioLib напрямую)
  if (doc.containsKey("outputPower")) {
    systemState.outputPower = doc["outputPower"].as<int>();
  }

  saveSystemState();

  resp["success"] = ok;
  resp["frequency"] = CC1101Manager::getFrequency();
  resp["bitRate"] = systemState.bitRate;
  resp["frequencyDeviation"] = systemState.freqDeviation;
  resp["rxBandwidth"] = systemState.rxBandwidth;
  resp["outputPower"] = systemState.outputPower;

  String response;
  serializeJson(resp, response);
  sendLog("Настройки CC1101 обновлены", "success");
  server.send(200, "application/json", response);
}

// --- Setup Function ---
void setup() {
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("Проект: Умные Ворота (Web + React)");
  Serial.println("ESP32 DevKit 38 pin");
  Serial.println("=================================");
  Serial.println("Запуск системы...");

  // Инициализация раздела userdata для хранения ключей/телефонов/настроек
  initUserDataPartition();

  // Загрузка состояния системы из постоянной памяти (раздел userdata)
  loadSystemState();
  

  // Инициализация GateControl
  GateControl::init(LED_PIN);
  Serial.println("[OK] GateControl инициализирован");

  // Инициализация CC1101
  Serial.println("[INIT] Инициализация CC1101 радиомодуля...");
  
  // Устанавливаем частоту по умолчанию, если не сохранена
  if (systemState.currentFrequency <= 0) {
    systemState.currentFrequency = 433.92; // Дефолтная частота
  }
  
  if (CC1101Manager::init(CC1101_CS, CC1101_GDO0, CC1101_GDO2)) {
    Serial.println("[OK] CC1101 успешно инициализирован");
    
    // Устанавливаем сохраненную/дефолтную частоту
    CC1101Manager::setFrequency(systemState.currentFrequency);
    Serial.println("[OK] Частота установлена: " + String(systemState.currentFrequency) + " МГц");
    Serial.println("[INFO] Первые 3 секунды сигналы будут игнорироваться (фильтрация начальных артефактов)");
  } else {
    Serial.println("[ERROR] Ошибка инициализации CC1101!");
  }
  Serial.println("[INFO] Ожидаем RF сигналы на частоте " + String(CC1101Manager::getFrequency()) + " МГц...");

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
  
  // Инициализация Logger (после WebSocket)
  Logger::init(&webSocket);
  Serial.println("[OK] Logger инициализирован");

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
  server.on("/api/keys/stop", HTTP_POST, handleKeysStop);
  server.on("/api/keys/status", HTTP_GET, handleKeysStatus);
  server.on("/api/keys/delete", HTTP_POST, handleKeysDelete);
  server.on("/api/keys/update", HTTP_PUT, handleKeyUpdate);
  server.on("/api/gate/trigger", HTTP_POST, handleGateTrigger);
  server.on("/api/frequency", HTTP_GET, handleFrequencyGet);
  server.on("/api/frequency/set", HTTP_POST, handleFrequencySet);
  server.on("/api/cc1101/config", HTTP_GET, handleCC1101Config);
  server.on("/api/cc1101/settings", HTTP_POST, handleCC1101Settings);
  server.on("/api/system/info", HTTP_GET, handleSystemInfo);
  server.on("/api/system/log", HTTP_GET, handleLogFile);
  
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
  sendLog("🔌 CC1101 активен на " + String(CC1101Manager::getFrequency()) + " МГц", "info");
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

  // RSSI стриминг в WebSocket (режим обучения — для визуализатора сигнала)
  static unsigned long lastRssiStream = 0;
  if (systemState.learningMode && millis() - lastRssiStream > 200) {
    lastRssiStream = millis();
    int rssi = CC1101Manager::getRSSI();
    String rssiEvent = "{\"rssi\":" + String(rssi) + ",\"freq\":" + String(CC1101Manager::getFrequency()) + "}";
    sendWebSocketEvent("rssi", rssiEvent.c_str());
  }

  // Очистка устаревших распознаваний (каждые 5 секунд)
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 5000) {
    cleanupOldRecognitions();
    cleanupDetectionHistory();
    lastCleanup = millis();
  }

  // Обработка CC1101 RF сигналов
  if (CC1101Manager::checkReceived()) {
    ReceivedKey receivedKey = CC1101Manager::getReceivedKey();
    
    if (receivedKey.code != 0) {
      // Используем улучшенное сравнение ключей
      KeyEntry* existingKey = nullptr;
      bool keyExists = false;
      
      for (auto& key : systemState.keys433) {
        if (isKeyMatch(key, receivedKey, receivedKey.bitString, receivedKey.bitLength, receivedKey.te)) {
          keyExists = true;
          existingKey = &key;
          break;
        }
      }
      
      if (systemState.learningMode) {
        // В режиме обучения принимаем ТОЛЬКО декодированные протоколы,
        // RAW/Unknown — это шум эфира, не сохраняем
        if (receivedKey.protocol == "RAW/Unknown" || receivedKey.protocol == "RAW/Custom") {
          Serial.printf("[CC1101] Обучение: пропускаем шум (%s, RSSI: %d)\n",
                        receivedKey.protocol.c_str(), receivedKey.rssi);
          // Отправляем в UI как сигнал (для спектрограммы), но не сохраняем
          String keyData = "{\"code\":" + String(receivedKey.code) +
                           ",\"rssi\":" + String(receivedKey.rssi) +
                           ",\"protocol\":\"" + receivedKey.protocol + "\"" +
                           ",\"bitLength\":" + String(receivedKey.bitLength) +
                           ",\"frequency\":" + String(CC1101Manager::getFrequency()) + "}";
          sendWebSocketEvent("key_received", keyData.c_str());
          CC1101Manager::resetReceived();
          return; // Выходим из loop(), обработаем следующий сигнал на следующей итерации
        }

        Serial.println("[CC1101] Режим обучения: декодирован " + receivedKey.protocol);
        sendLog("Обнаружен: " + receivedKey.protocol + " " + String(receivedKey.bitLength) + " бит", "info");

        // Режим обучения - добавляем новый ключ с полной информацией
        if (!keyExists) {
          KeyEntry newKey;
          newKey.code = receivedKey.code;
          // Генерируем имя на основе протокола
          if (receivedKey.protocol != "RAW/Unknown") {
            newKey.name = receivedKey.protocol + "-0x" + String(receivedKey.code, HEX);
          } else {
            newKey.name = "Ключ 0x" + String(receivedKey.code, HEX);
          }
          newKey.enabled = true;
          newKey.protocol = receivedKey.protocol;
          newKey.bitString = receivedKey.bitString;
          newKey.bitLength = receivedKey.bitLength;
          newKey.te = receivedKey.te;
          newKey.frequency = CC1101Manager::getFrequency();
          newKey.modulation = receivedKey.modulation;
          newKey.rawData = receivedKey.rawData;
          newKey.rssi = receivedKey.rssi;
          newKey.timestamp = receivedKey.timestamp;
          
          systemState.keys433.push_back(newKey);
          
          // Выключаем режим обучения
          systemState.learningMode = false;
          
          // Сохраняем состояние
          saveSystemState();
          
          Serial.println("[CC1101] ✅ Новый ключ добавлен: " + newKey.name);
          Serial.printf("[CC1101] Протокол: %s, Бит: %d, TE: %.1f мкс\n", 
                       newKey.protocol.c_str(), newKey.bitLength, newKey.te);
          sendLog("🔑 Новый ключ добавлен: " + newKey.name, "success");
          
          // Отправляем событие о добавлении ключа
          String keyData = "{\"code\":" + String(receivedKey.code) + 
                          ",\"name\":\"" + newKey.name + "\"" +
                          ",\"enabled\":" + String(newKey.enabled) +
                          ",\"protocol\":\"" + newKey.protocol + "\"" +
                          ",\"bitLength\":" + String(newKey.bitLength) +
                          ",\"rawData\":\"" + newKey.rawData + "\"" +
                          ",\"rssi\":" + String(newKey.rssi) +
                          ",\"frequency\":" + String(newKey.frequency) +
                          ",\"modulation\":\"" + newKey.modulation + "\"" +
                          ",\"timestamp\":" + String(newKey.timestamp) + "}";
          sendWebSocketEvent("key_added", keyData.c_str());

          String learnEvent = "{\"code\":" + String(receivedKey.code) +
                               ",\"rawData\":\"" + receivedKey.rawData + "\"" +
                               ",\"bitString\":\"" + receivedKey.bitString + "\"" +
                               ",\"bitLength\":" + String(receivedKey.bitLength) +
                               ",\"rssi\":" + String(receivedKey.rssi) +
                               ",\"snr\":" + String(receivedKey.snr) +
                               ",\"frequency\":" + String(CC1101Manager::getFrequency()) +
                               ",\"protocol\":\"" + receivedKey.protocol + "\"" +
                               ",\"modulation\":\"" + receivedKey.modulation + "\"" +
                               ",\"timestamp\":" + String(receivedKey.timestamp) +
                               ",\"hash\":" + String(receivedKey.hash) + "}";
          sendWebSocketEvent("key_received", learnEvent.c_str());
        } else {
          Serial.println("[CC1101] ⚠️ Ключ уже существует в режиме обучения");
          systemState.learningMode = false;
          saveSystemState();
          sendLog("⚠️ Ключ уже существует: " + existingKey->name, "warning");
          String learnEvent = "{\"code\":" + String(receivedKey.code) +
                               ",\"rawData\":\"" + receivedKey.rawData + "\"" +
                               ",\"bitString\":\"" + receivedKey.bitString + "\"" +
                               ",\"bitLength\":" + String(receivedKey.bitLength) +
                               ",\"rssi\":" + String(receivedKey.rssi) +
                               ",\"snr\":" + String(receivedKey.snr) +
                               ",\"frequency\":" + String(CC1101Manager::getFrequency()) +
                               ",\"protocol\":\"" + receivedKey.protocol + "\"" +
                               ",\"modulation\":\"" + receivedKey.modulation + "\"" +
                               ",\"timestamp\":" + String(receivedKey.timestamp) +
                               ",\"hash\":" + String(receivedKey.hash) + "}";
          sendWebSocketEvent("key_received", learnEvent.c_str());
        }
      } else {
        bool gateTriggered = false;
        bool hasSerialMessage = false;
        String serialMessage;
        bool hasLogMessage = false;
        String logMessage;
        const char* logType = nullptr;
        bool sendEventToUI = true;

        if (keyExists && existingKey != nullptr) {
          // Ключ найден в базе — активируем сразу без верификации (как Flipper Zero)
          if (existingKey->enabled) {
            gateTriggered = true;
            serialMessage = "[CC1101] ✅ Активация ворот ключом: " + existingKey->name +
                            " (RSSI: " + String(receivedKey.rssi) + " dBm, " + receivedKey.protocol + ")";
            hasSerialMessage = true;
            logMessage = "🚪 Ворота активированы: " + existingKey->name;
            logType = "success";
            hasLogMessage = true;
            GateControl::triggerGatePulse();
            systemState.gateOpenCount++;
            saveSystemState();
            RingLog::append(("Ворота: " + existingKey->name + " RSSI:" + String(receivedKey.rssi)).c_str());
          } else {
            serialMessage = "[CC1101] ⚠️ Ключ отключен: " + existingKey->name;
            hasSerialMessage = true;
            logMessage = "⚠️ Ключ отключен: " + existingKey->name;
            logType = "warning";
            hasLogMessage = true;
          }
        } else {
          // Неизвестный ключ — логируем только в Serial, не спамим WebSocket
          serialMessage = "[CC1101] ❓ Неизвестный ключ: " + receivedKey.protocol +
                          " 0x" + String(receivedKey.code, HEX) +
                          " (RSSI: " + String(receivedKey.rssi) + " dBm)";
          hasSerialMessage = true;
          hasLogMessage = false;
        }

        bool suppressDuplicate = isDuplicateForDisplay(receivedKey);
        if (gateTriggered) {
          suppressDuplicate = false;
        }

        if (suppressDuplicate) {
          Serial.printf("[CC1101] 🔁 Дубликат сигнала: %s 0x%X (подавлен)\n",
                        receivedKey.protocol.c_str(), receivedKey.code);
          sendEventToUI = false;
          hasSerialMessage = false;
          hasLogMessage = false;
        }

        if (hasSerialMessage) {
          Serial.println(serialMessage);
        }

        if (hasLogMessage && logType != nullptr) {
          sendLog(logMessage, logType);
        }

        if (!suppressDuplicate && sendEventToUI) {
          String keyData = "{\"code\":" + String(receivedKey.code) + 
                           ",\"rawData\":\"" + receivedKey.rawData + "\"" +
                           ",\"bitString\":\"" + receivedKey.bitString + "\"" +
                           ",\"bitLength\":" + String(receivedKey.bitLength) +
                           ",\"rssi\":" + String(receivedKey.rssi) +
                           ",\"snr\":" + String(receivedKey.snr) +
                           ",\"frequency\":" + String(CC1101Manager::getFrequency()) +
                           ",\"protocol\":\"" + receivedKey.protocol + "\"" +
                           ",\"modulation\":\"" + receivedKey.modulation + "\"" +
                           ",\"timestamp\":" + String(receivedKey.timestamp) +
                           ",\"hash\":" + String(receivedKey.hash) + "}";
          sendWebSocketEvent("key_received", keyData.c_str());
        }
      }
    }
    
    CC1101Manager::resetReceived();
  }
  
  // Периодическая диагностика CC1101 (каждые 30 секунд)
  static unsigned long lastDiagnostic = 0;
  if (millis() - lastDiagnostic > 30000) {
    lastDiagnostic = millis();
    int rssi = CC1101Manager::getRSSI();
    Serial.println("[CC1101] Диагностика - RSSI: " + String(rssi) + " dBm, Частота: " + String(CC1101Manager::getFrequency()) + " МГц");
  }

  // Обработка GSM (опционально)
  // Раскомментируйте, когда GSM модуль будет подключен
  // GSMManager::handleGSM();
}