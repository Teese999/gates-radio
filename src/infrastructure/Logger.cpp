#include "Logger.h"
#include <stdio.h>
#include <stdarg.h>

// Статическая переменная для хранения экземпляра WebSocket
WebSocketsServer* Logger::webSocketInstance = nullptr;

void Logger::init(WebSocketsServer* ws) {
    webSocketInstance = ws;
}

// Вспомогательная функция для экранирования JSON строк
static String escapeJsonString(const String& str) {
    String escaped;
    escaped.reserve(str.length() + 10); // Резервируем немного больше места
    
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:   escaped += c; break;
        }
    }
    return escaped;
}

void Logger::log(String message, const char* type) {
    // Выводим в Serial
    Serial.printf("[%s] %s\n", type, message.c_str());
    
    // Отправляем через WebSocket, если он инициализирован
    if (webSocketInstance != nullptr) {
        String escapedMessage = escapeJsonString(message);
        String logData = "{\"message\":\"" + escapedMessage + "\",\"type\":\"" + String(type) + "\"}";
        String json = "{\"event\":\"log\",\"data\":" + logData + "}";
        webSocketInstance->broadcastTXT(json);
    }
}

void Logger::info(String message) {
    log(message, "info");
}

void Logger::success(String message) {
    log(message, "success");
}

void Logger::warning(String message) {
    log(message, "warning");
}

void Logger::error(String message) {
    log(message, "error");
}

void Logger::sendLog(String message, const char* type) {
    log(message, type);
}

void Logger::logf(const char* type, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    log(String(buffer), type);
}

