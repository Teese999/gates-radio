#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#include <WebSocketsServer.h>

// Универсальная система логирования
// Выводит логи в Serial и отправляет через WebSocket на фронтенд
class Logger {
public:
    // Инициализация (опционально, для настройки WebSocket)
    static void init(WebSocketsServer* ws);
    
    // Основная функция логирования
    // type: "info", "success", "warning", "error"
    static void log(String message, const char* type = "info");
    
    // Удобные функции для разных типов логов
    static void info(String message);
    static void success(String message);
    static void warning(String message);
    static void error(String message);
    
    // Для совместимости со старым кодом
    static void sendLog(String message, const char* type = "info");
    
    // Форматированное логирование (как Serial.printf)
    static void logf(const char* type, const char* format, ...);
    
private:
    static WebSocketsServer* webSocketInstance;
};

#endif // LOGGER_H

